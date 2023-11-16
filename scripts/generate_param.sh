#!/bin/bash

cat << EOF
#define NPROC $1
#define KSTACKSIZE 4096
#define NCPU $2
#define NOFILE 16
#define NFILE 100
#define NINODE 50
#define NDEV 10
#define ROOTDEV 1
#define MAXARG 32
#define MAXOPBLOCKS 10
#define LOGSIZE (MAXOPBLOCKS * 3)
#define NBUF (MAXOPBLOCKS * 3)
#define FSSIZE 1000
#define LOG_SIZE $3
EOF