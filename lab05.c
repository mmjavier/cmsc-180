/*
@author: Myko Jefferson M. Javier
@date: April 30, 2026
@section: CMSC 180 - CD3L
@code-desc: Socketed Min-Max Transformation using Tree-based Broadcasting
*/

/*
 * DESIGN: Single-port, bidirectional tree protocol
 * ─────────────────────────────────────────────────
 * Every tree edge uses ONE TCP connection opened by the parent/master.
 * The same socket is reused in both phases:
 *
 *   SCATTER  (top-down)  : parent writes scatter chunk  → child reads it
 *   REDUCTION (bottom-up): child  writes result chunk   → parent reads it
 *
 * Because the parent holds the socket open after sending the scatter
 * chunk, it simply recv()s the result back on the same fd once the
 * child is done.  There are no extra ports, no new connect() calls
 * during the reduction phase, and therefore no race conditions.
 *
 * tree_node() recurses as follows per slave:
 *   1. Receive my subtree chunk from parent (via parent_sock)
 *   2. Connect to left child, send its chunk  → left_sock stays open
 *   3. Connect to right child, send its chunk → right_sock stays open
 *   4. Do MMT on my own assigned columns
 *   5. Recv left child's assembled result on left_sock,  close it
 *   6. Recv right child's assembled result on right_sock, close it
 *   7. Send fully assembled subtree result back on parent_sock
 *
 * Steps 2-3 fan out O(log t) levels; steps 5-6 fan back in the same
 * O(log t) levels.  Total connections = 2*(t-1) = O(t).
 *
 * NOTE: each slave process is launched independently and accepts
 * exactly ONE inbound connection (from its parent or master) on its
 * configured port, then hands off to tree_node().  No second server
 * socket is needed anywhere.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#ifdef __linux__
#include <sched.h>
#endif

#define CONFIG_FILE "config.txt"

typedef struct { char ip[16]; int port; } SlaveConfig;

/* ── prototypes ─────────────────────────────────────────────────── */
int      create_server_socket(int port);
int      connect_to_slave(const char *ip, int port);
void     send_matrix_chunk(int sock, double **mat, int rows, int cols, int col_off);
void     recv_matrix_chunk(int sock, double **mat, int rows, int cols, int col_off);
void     send_int_v(int sock, int v);
void     recv_int_v(int sock, int *v);
void     mmt(double **mat, int n, int sc, int nc);
double   col_max(double **mat, int n, int col);
double   col_min(double **mat, int n, int col);
double   rnd(int max);
void     print_matrix(double **mat, int rows, int cols);
double **alloc_matrix(int rows, int cols);
void     free_matrix(double **mat, int rows);
int64_t  ts_now(void);
double   ts_sec(int64_t t);
int      read_config(SlaveConfig **slaves, int *n);
int      col_start(int rank, int n, int t);
int      col_count(int rank, int n, int t);
void     tree_node(int parent_sock, int gs, int ge, int n, int t,
                   SlaveConfig *slaves, int server_sock);

/* ═══════════════════════════════════════════════════════════════════
   MAIN
   ═══════════════════════════════════════════════════════════════════ */
int main(int argc, char **argv)
{
    int n = 0, p = 0, s = -1;
    char *filename = NULL;

    if (argc >= 4) {
        n = atoi(argv[1]);
        if (n == 0 && strcmp(argv[1], "0") != 0) filename = argv[1];
        p = atoi(argv[2]);
        s = atoi(argv[3]);
    } else {
        printf("Enter matrix dimension (n): ");
        if (scanf("%d", &n) != 1) { fprintf(stderr, "bad n\n"); return 1; }
        printf("Enter port (p): ");
        if (scanf("%d", &p) != 1) { fprintf(stderr, "bad p\n"); return 1; }
        printf("Enter status (0=master,1=slave): ");
        if (scanf("%d", &s) != 1) { fprintf(stderr, "bad s\n"); return 1; }
    }

    /* ── MASTER ─────────────────────────────────────────────────── */
    if (s == 0) {
        printf("\n=== MASTER PROCESS ===\n");

        double **matrix = NULL;
        if (filename) {
            FILE *f = fopen(filename, "r");
            if (!f) { perror("fopen"); return 1; }
            if (fscanf(f, "%d", &n) != 1) { fprintf(stderr,"bad dim\n"); return 1; }
            matrix = alloc_matrix(n, n);
            for (int i = 0; i < n; i++)
                for (int j = 0; j < n; j++)
                    if (fscanf(f, "%lf", &matrix[i][j]) != 1) {
                        fprintf(stderr,"bad [%d][%d]\n",i,j);
                        fclose(f); return 1;
                    }
            fclose(f);
            printf("Loaded %dx%d from %s\n", n, n, filename);
        } else {
            if (n <= 0) { fprintf(stderr,"invalid n\n"); return 1; }
            matrix = alloc_matrix(n, n);
            srand((unsigned)time(NULL));
            for (int i = 0; i < n; i++)
                for (int j = 0; j < n; j++)
                    matrix[i][j] = rnd(100);
            printf("Generated %dx%d random matrix\n", n, n);
        }

        SlaveConfig *slaves = NULL; int t = 0;
        if (read_config(&slaves, &t) != 0) {
            printf("No config – running locally\n");
            int64_t a = ts_now(); mmt(matrix, n, 0, n); int64_t b = ts_now();
            printf("Local MMT: %lf s\n", ts_sec(b-a));
            print_matrix(matrix, n, n);
            free_matrix(matrix, n); return 0;
        }
        printf("Slaves: %d\n", t);
        for (int i = 0; i < t; i++)
            printf("  [%d] %s:%d\n", i, slaves[i].ip, slaves[i].port);

        /* ── master wall-clock starts here ── */
        int64_t t0 = ts_now();

        /*
         * Protocol master→slave0 (same socket for both directions):
         *   SEND: n, t, group_start=0, group_end=t-1
         *   SEND: full n×n matrix
         *   RECV: full n×n transformed matrix
         */
        int sock0 = connect_to_slave(slaves[0].ip, slaves[0].port);
        if (sock0 < 0) { fprintf(stderr,"cannot connect to slave 0\n"); return 1; }

        send_int_v(sock0, n);
        send_int_v(sock0, t);
        send_int_v(sock0, 0);
        send_int_v(sock0, t - 1);
        send_matrix_chunk(sock0, matrix, n, n, 0);
        printf("Sent full %dx%d matrix to slave 0\n", n, n);

        printf("Waiting for transformed matrix ...\n");
        recv_matrix_chunk(sock0, matrix, n, n, 0);
        close(sock0);

        int64_t t1 = ts_now();
        printf("\n=== Master wall-clock: %lf seconds ===\n", ts_sec(t1-t0));
        printf("\nTransformed matrix:\n");
        print_matrix(matrix, n, n);

        free(slaves); free_matrix(matrix, n);

    /* ── SLAVE ──────────────────────────────────────────────────── */
    } else if (s == 1) {
        printf("\n=== SLAVE PROCESS (port %d) ===\n", p);

        int server_sock = create_server_socket(p);
        if (server_sock < 0) return 1;
        listen(server_sock, 10);
        printf("Listening on port %d\n", p);

        SlaveConfig *slaves = NULL; int t = 0;
        if (read_config(&slaves, &t) != 0) {
            fprintf(stderr,"cannot read config\n"); return 1;
        }

        /* Accept exactly ONE connection (from parent or master) */
        struct sockaddr_in peer; socklen_t plen = sizeof peer;
        int parent_sock = accept(server_sock, (struct sockaddr *)&peer, &plen);
        if (parent_sock < 0) { perror("accept"); return 1; }
        printf("Connected from %s\n", inet_ntoa(peer.sin_addr));

        int n_r, t_r, gs, ge;
        recv_int_v(parent_sock, &n_r);
        recv_int_v(parent_sock, &t_r);
        recv_int_v(parent_sock, &gs);
        recv_int_v(parent_sock, &ge);

        tree_node(parent_sock, gs, ge, n_r, t_r, slaves, server_sock);

        close(parent_sock);
        close(server_sock);
        free(slaves);
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
   TREE_NODE
   ═══════════════════════════════════════════════════════════════════

   Receives a subtree chunk from parent_sock, scatters to children
   (keeping those sockets open), does its own MMT, then collects
   children results on the same sockets and returns the assembled
   result back on parent_sock.  Pure recursion — no extra ports.

   Each child process runs concurrently.  After we send scatter data
   on left_sock / right_sock, the children independently recurse into
   THEIR sub-trees.  Meanwhile we do our own MMT.  We then block on
   recv() from left and right sockets in turn — they arrive whenever
   the child subtree finishes.  This is the natural backtracking step.
 */
void tree_node(int parent_sock,
               int gs, int ge,
               int n, int t,
               SlaveConfig *slaves,
               int server_sock)
{
    int rank = gs;

    /* ── CPU affinity ─────────────────────────────────────────── */
#ifdef __linux__
    {
        int nc = (int)sysconf(_SC_NPROCESSORS_ONLN);
        int av = nc > 1 ? nc - 1 : 1;
        int cpu = rank % av;
        cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(cpu, &cs);
        if (sched_setaffinity(0, sizeof cs, &cs) == 0)
            printf("Slave %d pinned to core %d\n", rank, cpu);
    }
#endif

    /* ── Column ranges ────────────────────────────────────────── */
    int sub_col_s  = col_start(gs, n, t);
    int sub_col_e  = col_start(ge, n, t) + col_count(ge, n, t) - 1;
    int sub_cols   = sub_col_e - sub_col_s + 1;
    int my_cols    = col_count(rank, n, t);

    printf("Slave %d: group [%d..%d] => global cols [%d..%d], my_cols=%d\n",
           rank, gs, ge, sub_col_s, sub_col_e, my_cols);

    /* ── Step 1: receive subtree chunk from parent ────────────── */
    /*
     * Layout: local column 0 = global col sub_col_s.
     * My own columns are at local indices [0 .. my_cols-1].
     * Children's columns follow at [my_cols .. sub_cols-1].
     */
    double **sub = alloc_matrix(n, sub_cols);
    recv_matrix_chunk(parent_sock, sub, n, sub_cols, 0);
    printf("Slave %d: received %d-col chunk\n", rank, sub_cols);

    /* ── Steps 2-3: scatter to children, keep sockets open ────── */
    int left_sock  = -1, left_gs  = -1, left_ge  = -1;
    int right_sock = -1, right_gs = -1, right_ge = -1;

    if (gs < ge) {
        int rem_s = gs + 1, rem_e = ge;
        int mid   = rem_s + (rem_e - rem_s) / 2;

        /* LEFT child covers ranks [rem_s .. mid] */
        left_gs = rem_s; left_ge = mid;
        {
            int lcs = col_start(left_gs, n, t);
            int lce = col_start(left_ge, n, t) + col_count(left_ge, n, t) - 1;
            int lcc = lce - lcs + 1;
            int lco = lcs - sub_col_s;   /* local offset in sub[] */

            left_sock = connect_to_slave(slaves[left_gs].ip, slaves[left_gs].port);
            if (left_sock >= 0) {
                send_int_v(left_sock, n);
                send_int_v(left_sock, t);
                send_int_v(left_sock, left_gs);
                send_int_v(left_sock, left_ge);
                send_matrix_chunk(left_sock, sub, n, lcc, lco);
                printf("Slave %d -> L Slave %d (cols %d..%d)\n",
                       rank, left_gs, lcs, lce);
            } else {
                fprintf(stderr,"Slave %d: cannot connect to left child %d\n",
                        rank, left_gs);
            }
        }

        /* RIGHT child covers ranks [mid+1 .. rem_e] */
        if (mid + 1 <= rem_e) {
            right_gs = mid + 1; right_ge = rem_e;
            int rcs = col_start(right_gs, n, t);
            int rce = col_start(right_ge, n, t) + col_count(right_ge, n, t) - 1;
            int rcc = rce - rcs + 1;
            int rco = rcs - sub_col_s;

            right_sock = connect_to_slave(slaves[right_gs].ip, slaves[right_gs].port);
            if (right_sock >= 0) {
                send_int_v(right_sock, n);
                send_int_v(right_sock, t);
                send_int_v(right_sock, right_gs);
                send_int_v(right_sock, right_ge);
                send_matrix_chunk(right_sock, sub, n, rcc, rco);
                printf("Slave %d -> R Slave %d (cols %d..%d)\n",
                       rank, right_gs, rcs, rce);
            } else {
                fprintf(stderr,"Slave %d: cannot connect to right child %d\n",
                        rank, right_gs);
            }
        }
    }

    /*
     * Children are now running independently in their own processes.
     * They will recurse into their sub-trees, do their MMT, assemble
     * results, and then send back on the socket we just opened.
     *
     * Meanwhile, we do OUR own MMT (step 4).
     */

    /* ── Step 4: MMT on my own columns (local indices 0..my_cols-1) */
    int64_t mmt_s = ts_now();
    mmt(sub, n, 0, my_cols);
    int64_t mmt_e = ts_now();

    printf("Slave %d MMT time: %lf seconds\n", rank, ts_sec(mmt_e - mmt_s));
    printf("Slave %d transformed submatrix (own %d cols):\n", rank, my_cols);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < my_cols; j++) printf("%8.4f ", sub[i][j]);
        printf("\n");
    }

    /* ── Step 5: receive left child result (backtrack) ──────────── */
    if (left_sock >= 0) {
        int lcs = col_start(left_gs, n, t);
        int lce = col_start(left_ge, n, t) + col_count(left_ge, n, t) - 1;
        int lcc = lce - lcs + 1;
        int lco = lcs - sub_col_s;
        recv_matrix_chunk(left_sock, sub, n, lcc, lco);
        close(left_sock);
        printf("Slave %d: collected left [%d..%d] result (%d cols)\n",
               rank, left_gs, left_ge, lcc);
    }

    /* ── Step 6: receive right child result (backtrack) ─────────── */
    if (right_sock >= 0) {
        int rcs = col_start(right_gs, n, t);
        int rce = col_start(right_ge, n, t) + col_count(right_ge, n, t) - 1;
        int rcc = rce - rcs + 1;
        int rco = rcs - sub_col_s;
        recv_matrix_chunk(right_sock, sub, n, rcc, rco);
        close(right_sock);
        printf("Slave %d: collected right [%d..%d] result (%d cols)\n",
               rank, right_gs, right_ge, rcc);
    }

    /* ── Step 7: send assembled subtree back to parent ──────────── */
    send_matrix_chunk(parent_sock, sub, n, sub_cols, 0);
    printf("Slave %d: sent %d-col assembled result back\n", rank, sub_cols);

    free_matrix(sub, n);
}

/* ═══════════════════════════════════════════════════════════════════
   PARTITION HELPERS
   ═══════════════════════════════════════════════════════════════════ */
int col_start(int rank, int n, int t) {
    int b = n/t, e = n%t;
    return rank*b + (rank < e ? rank : e);
}
int col_count(int rank, int n, int t) {
    int b = n/t, e = n%t;
    return b + (rank < e ? 1 : 0);
}

/* ═══════════════════════════════════════════════════════════════════
   NETWORK HELPERS
   ═══════════════════════════════════════════════════════════════════ */
int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) { perror("bind"); close(fd); return -1; }
    return fd;
}

int connect_to_slave(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET; a.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &a.sin_addr) <= 0) { perror("inet_pton"); close(fd); return -1; }
    for (int tries = 30; tries > 0; tries--) {
        if (connect(fd, (struct sockaddr *)&a, sizeof a) == 0) return fd;
        sleep(1);
    }
    perror("connect"); close(fd); return -1;
}

void send_matrix_chunk(int sock, double **mat, int rows, int cols, int col_off) {
    for (int i = 0; i < rows; i++) {
        int total = 0, need = cols * (int)sizeof(double);
        while (total < need) {
            int r = send(sock, (char *)&mat[i][col_off] + total, need - total, 0);
            if (r <= 0) { perror("send_matrix_chunk"); return; }
            total += r;
        }
    }
}

void recv_matrix_chunk(int sock, double **mat, int rows, int cols, int col_off) {
    for (int i = 0; i < rows; i++) {
        int total = 0, need = cols * (int)sizeof(double);
        while (total < need) {
            int r = recv(sock, (char *)&mat[i][col_off] + total, need - total, 0);
            if (r <= 0) { perror("recv_matrix_chunk"); return; }
            total += r;
        }
    }
}

void send_int_v(int sock, int v) {
    int total = 0;
    while (total < (int)sizeof v) {
        int r = send(sock, (char *)&v + total, sizeof(v) - total, 0);
        if (r <= 0) { perror("send_int"); return; }
        total += r;
    }
}

void recv_int_v(int sock, int *v) {
    int total = 0;
    while (total < (int)sizeof *v) {
        int r = recv(sock, (char *)v + total, sizeof(*v) - total, 0);
        if (r <= 0) { perror("recv_int"); return; }
        total += r;
    }
}

/* ═══════════════════════════════════════════════════════════════════
   CONFIG
   ═══════════════════════════════════════════════════════════════════ */
int read_config(SlaveConfig **slaves, int *n) {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) { perror("fopen config.txt"); return -1; }
    fscanf(f, "%d", n);
    *slaves = malloc(sizeof(SlaveConfig) * (*n));
    for (int i = 0; i < *n; i++)
        fscanf(f, "%s %d", (*slaves)[i].ip, &(*slaves)[i].port);
    fclose(f); return 0;
}

/* ═══════════════════════════════════════════════════════════════════
   MMT
   ═══════════════════════════════════════════════════════════════════ */
double col_max(double **mat, int n, int col) {
    double m = mat[0][col];
    for (int i = 1; i < n; i++) if (mat[i][col] > m) m = mat[i][col];
    return m;
}
double col_min(double **mat, int n, int col) {
    double m = mat[0][col];
    for (int i = 1; i < n; i++) if (mat[i][col] < m) m = mat[i][col];
    return m;
}
void mmt(double **mat, int n, int sc, int nc) {
    for (int k = 0; k < nc; k++) {
        int c = sc + k;
        double hi = col_max(mat, n, c), lo = col_min(mat, n, c);
        for (int j = 0; j < n; j++)
            mat[j][c] = (hi == lo) ? 0.0 : (mat[j][c] - lo) / (hi - lo);
    }
}

/* ═══════════════════════════════════════════════════════════════════
   UTILITIES
   ═══════════════════════════════════════════════════════════════════ */
double rnd(int max) { return (rand() % max) + 1; }

void print_matrix(double **mat, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) printf("%8.4f ", mat[i][j]);
        printf("\n");
    }
}

double **alloc_matrix(int rows, int cols) {
    double **m = malloc(sizeof(double *) * rows);
    for (int i = 0; i < rows; i++) m[i] = malloc(sizeof(double) * cols);
    return m;
}

void free_matrix(double **mat, int rows) {
    for (int i = 0; i < rows; i++) free(mat[i]);
    free(mat);
}

int64_t ts_now(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * CLOCKS_PER_SEC + tv.tv_usec;
}
double ts_sec(int64_t t) { return t / (double)CLOCKS_PER_SEC; }