#include <intrin.h> 
#include <stdio.h>
#include <memory.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>

/*
 * global definitions
 */

typedef unsigned __int32    U32;
typedef unsigned __int64    U64;

#define N_LOOPS             (128) /* loops per measure */

#define BS_IN_BITS          (20 + 7) /* 128M bytes */
#define BS_IN_BYTES         (1UL << BS_IN_BITS)
#define BS_IN_DWORD         (BS_IN_BYTES >> 2)
#define BS_IN_QWORD         (BS_IN_BYTES >> 3)
#ifdef _M_AMD64
#define PTR_BITS            (3) /* sizeof(void *): 8 bytes */
#else
#define PTR_BITS            (2) /* sizeof(void *): 4 bytes */
#endif
#define PTR_BYTES           (1UL << PTR_BITS)
#define PTR_MASK            (PTR_BYTES - 1)
#define BS_IN_PTRS          (BS_IN_BYTES >> PTR_BITS)


#define rdtscp(ui)          __rdtscp(ui)
//#define rdtscp(ui)        (_mm_lfence(),__rdtsc())

#define CALC_MIN(v, s, b)   if ((b) > (s) && (v) > ((b) - (s))) (v) = ((b) - (s))
#define CALC_INITV          (MAXUINT64)

#define LOGI(format, ...)                                                   \
    do {                                                                    \
        printf_s(format, ##__VA_ARGS__);                                    \
    } while(0)

#define LOGD(format, ...)   do {} while(0)

/*
 * global variables
 */

PUCHAR  g_raw = NULL;
PUCHAR  g_ptr = NULL;
U64     g_tsc = CALC_INITV;

int cl_init()
{
    g_raw = (PUCHAR)VirtualAlloc(NULL, BS_IN_BYTES + 1048576,
                                 MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_raw)
        return -ENOMEM;

    /* round to 1M boundary */
    g_ptr = &g_raw[1048576 - ((ULONG_PTR)g_raw & (1048576 - 1))];
    LOGD("Memory Allocated:\n  g_raw = %p g_ptr = %p\n", g_raw, g_ptr);

    /* init buffer to warm up our friends: CPU cores */
    srand((unsigned int)time(NULL));
    for (int i = 0; i < BS_IN_PTRS; i++) {
        *((ULONG_PTR *)&g_ptr[i << PTR_BITS]) = (ULONG_PTR)rand() * rand();
    }

    return 0;
}

void cl_memclr()
{
    memset(g_raw, 0xF0, BS_IN_BYTES + 1048576);
}

void cl_measure_ins()
{
    U64 csci[16], csci2[3] = {CALC_INITV, CALC_INITV, CALC_INITV };
    
    PUCHAR   data = g_ptr;
    register U32 i, j, t, m = rand(), ui;
    register U64 ts, te, tv = 0;
    float    f = 2.56f;

    for (j = 0; j < 16; j++)
        csci[j] = CALC_INITV;

    /* measure rdtscp latency */
    Sleep(0);
    for (t = 0; t < N_LOOPS * 4; t++) {
        ts = rdtscp(&ui);
        te = rdtscp(&ui);
        CALC_MIN(g_tsc, ts, te);
    }
    LOGI("Real Time Clock Latency: %I64u\n", g_tsc);

    for (t = 0; t < N_LOOPS; t++) {

        /* flush cache line */
        _mm_clflush(&data[0]);

        /* measure cache miss latency */
        ts = rdtscp(&ui);
        m |= data[0];
        te = rdtscp(&ui);
        CALC_MIN(csci[0], ts, te);

        /* measure cache hit latency */
        ts = rdtscp(&ui);
        m &= data[0];
        te = rdtscp(&ui);
        CALC_MIN(csci[1], ts, te);

        /* measure mul latency */
        ts = rdtscp(&ui);
        m *= *((U32 *)&data[0]);
        te = rdtscp(&ui);
        CALC_MIN(csci[2], ts, te);

        /* measure div latnecy */
        ts = rdtscp(&ui);
        m /= *((U32 *)&data[0]);
        te = rdtscp(&ui);
        CALC_MIN(csci[3], ts, te);

        /* measure 2*mul latnecy */
        ts = rdtscp(&ui);
        m *= *((U32 *)&data[0]);
        m *= *((U32 *)&data[0]);
        te = rdtscp(&ui);
        CALC_MIN(csci2[0], ts, te);

        /* double div */
        ts = rdtscp(&ui);
        m /= *((U32 *)&data[0]);
        m /= *((U32 *)&data[0]);
        te = rdtscp(&ui);
        CALC_MIN(csci2[1], ts, te);

        /* mul + div */
        ts = rdtscp(&ui);
        m *= *((U32 *)&data[0]);
        m /= *((U32 *)&data[0]);
        te = rdtscp(&ui);
        CALC_MIN(csci2[2], ts, te);

        /* measure float mul latency */
        ts = rdtscp(&ui);
        f = f * m;
        te = rdtscp(&ui);
        CALC_MIN(csci[4], ts, te);

        /* measure float div latency */
        while (!m)
            m = rand();
        ts = rdtscp(&ui);
        f = f / m;
        te = rdtscp(&ui);
        CALC_MIN(csci[5], ts, te);
    }

    LOGI("Cache miss: %I64u (%I64u - %I64u)  Cache hit: %I64u (%I64u - %I64u)\n",
          (csci[0] - g_tsc), csci[0], g_tsc,
          (csci[1] <= g_tsc) ? 0 : (csci[1] - g_tsc),
          csci[1], g_tsc);

    LOGI("Instruction mul: %I64u (%I64u - %I64u)\n", 
         (csci[2] <= g_tsc) ? 0 : (csci[2] - g_tsc), csci[2], g_tsc);

    LOGI("Instruction div: %I64u (%I64u - %I64u)\n",
         (csci[3] <= g_tsc) ? 0 : (csci[3] - g_tsc), csci[3], g_tsc);

    LOGI("Instruction float mul: %I64u (%I64u - %I64u)\n",
         (csci[4] <= g_tsc) ? 0 : (csci[4] - g_tsc), csci[4], g_tsc);

    LOGI("Instruction float div: %I64u (%I64u - %I64u)\n",
         (csci[5] <= g_tsc) ? 0 : (csci[5] - g_tsc), csci[5], g_tsc);

    LOGI("Instruction double mul: %I64u (%I64u - %I64u)\n",
        (csci2[0] <= g_tsc) ? 0 : (csci2[0] - g_tsc), csci2[0], g_tsc);

    LOGI("Instruction double div: %I64u (%I64u - %I64u)\n",
        (csci2[1] <= g_tsc) ? 0 : (csci2[1] - g_tsc), csci2[1], g_tsc);

    LOGI("Instruction mul+div: %I64u (%I64u - %I64u)\n",
        (csci2[2] <= g_tsc) ? 0 : (csci2[2] - g_tsc), csci2[2], g_tsc);

    csci[6] = CALC_INITV;
    for (t = 0; t < N_LOOPS; t++) {
        /* measure 2 layer empty-loops: case 1 */
        ts = rdtscp(&ui);
        for (i = (1UL << 23); i > 0; i--)
            for (j = (1UL << 0); j > 0; j--);
        te = rdtscp(&ui);
        CALC_MIN(csci[6], ts, te);
    }
    LOGI("2 layer empty loops (%u * %u): %I64u (%I64u - %I64u)\n",
         (1UL << 23), (1UL << 0),
         (csci[6] <= g_tsc) ? 0 : (csci[6] - g_tsc), csci[6], g_tsc);

    csci[6] = CALC_INITV;
    for (t = 0; t < N_LOOPS; t++) {
        /* measure 2 layer empty-loops: case 1 */
        ts = rdtscp(&ui);
        for (i = (1UL << 22); i > 0; i--)
            for (j = (1UL << 1); j > 0; j--);
        te = rdtscp(&ui);
        CALC_MIN(csci[6], ts, te);
    }
    LOGI("2 layer empty loops (%u * %u): %I64u (%I64u - %I64u)\n",
        (1UL << 22), (1UL << 1),
        (csci[6] <= g_tsc) ? 0 : (csci[6] - g_tsc), csci[6], g_tsc);

    csci[6] = CALC_INITV;
    for (t = 0; t < N_LOOPS; t++) {
        /* measure 2 layer empty-loops: case 1 */
        ts = rdtscp(&ui);
        for (i = (1UL << 21); i > 0; i--)
            for (j = (1UL << 2); j > 0; j--);
        te = rdtscp(&ui);
        CALC_MIN(csci[6], ts, te);
    }
    LOGI("2 layer empty loops (%u * %u): %I64u (%I64u - %I64u)\n",
        (1UL << 21), (1UL << 2),
        (csci[6] <= g_tsc) ? 0 : (csci[6] - g_tsc), csci[6], g_tsc);

    csci[6] = CALC_INITV;
    for (t = 0; t < N_LOOPS; t++) {
        /* measure 2 layer empty-loops: case 1 */
        ts = rdtscp(&ui);
        for (i = (1UL << 20); i > 0; i--)
            for (j = (1UL << 3); j > 0; j--);
        te = rdtscp(&ui);
        CALC_MIN(csci[6], ts, te);
    }
    LOGI("2 layer empty loops (%u * %u): %I64u (%I64u - %I64u)\n",
        (1UL << 20), (1UL << 3),
        (csci[6] <= g_tsc) ? 0 : (csci[6] - g_tsc), csci[6], g_tsc);

    csci[6] = CALC_INITV;
    for (t = 0; t < N_LOOPS; t++) {
        /* measure 2 layer empty-loops: case 1 */
        ts = rdtscp(&ui);
        for (i = (1UL << 19); i > 0; i--)
            for (j = (1UL << 4); j > 0; j--);
        te = rdtscp(&ui);
        CALC_MIN(csci[6], ts, te);
    }
    LOGI("2 layer empty loops (%u * %u): %I64u (%I64u - %I64u)\n",
        (1UL << 19), (1UL << 4),
        (csci[6] <= g_tsc) ? 0 : (csci[6] - g_tsc), csci[6], g_tsc);

    csci[6] = CALC_INITV;
    for (t = 0; t < N_LOOPS; t++) {
        /* measure 2 layer empty-loops: case 1 */
        ts = rdtscp(&ui);
        for (i = (1UL << 18); i > 0; i--)
            for (j = (1UL << 5); j > 0; j--);
        te = rdtscp(&ui);
        CALC_MIN(csci[6], ts, te);
    }
    LOGI("2 layer empty loops (%u * %u): %I64u (%I64u - %I64u)\n",
        (1UL << 18), (1UL << 5),
        (csci[6] <= g_tsc) ? 0 : (csci[6] - g_tsc), csci[6], g_tsc);


    csci[6] = CALC_INITV;
    for (t = 0; t < N_LOOPS; t++) {
        /* measure 2 layer empty-loops: case 1 */
        ts = rdtscp(&ui);
        for (i = (1UL << 17); i > 0; i--)
            for (j = (1UL << 6); j > 0; j--);
        te = rdtscp(&ui);
        CALC_MIN(csci[6], ts, te);
    }
    LOGI("2 layer empty loops (%u * %u): %I64u (%I64u - %I64u)\n",
        (1UL << 17), (1UL << 6),
        (csci[6] <= g_tsc) ? 0 : (csci[6] - g_tsc), csci[6], g_tsc);


    csci[6] = CALC_INITV;
    for (t = 0; t < N_LOOPS; t++) {
        /* measure 2 layer empty-loops: case 1 */
        ts = rdtscp(&ui);
        for (i = (1UL << 20); i > 0; i--)
            for (j = (1UL << 3); j > 0; j--);
        te = rdtscp(&ui);
        CALC_MIN(csci[6], ts, te);

        /* measure 2 layer empty-loops: case 2 */
        ts = rdtscp(&ui);
        for (j = (1UL << 3); j > 0; j--)
            for (i = (1UL << 20); i > 0; i--);
        //        for (j = 0; j < (1UL << 3); j++)
//            for (i = 0; i < (1UL << 20); i++);
        te = rdtscp(&ui);
        CALC_MIN(csci[7], ts, te);
    }

    LOGI("2 layer empty loops (1M * 8): %I64u (%I64u - %I64u)\n",
        (csci[6] <= g_tsc) ? 0 : (csci[6] - g_tsc), csci[6], g_tsc);

    LOGI("2 layer empty loops (8 * 1M): %I64u (%I64u - %I64u)\n",
        (csci[7] <= g_tsc) ? 0 : (csci[7] - g_tsc), csci[7], g_tsc);

    for (t = 0; t < 100; t++) {

        /* measure 2 layer memory access loops: case 1 */
        ts = rdtscp(&ui);
        for (i = 0; i < (1UL << 20); i++)
            for (j = 0; j < 8; j++)
                tv += *((U64 *)&data[(j + (i << 3)) << 3]);
        te = rdtscp(&ui);
        CALC_MIN(csci[8], ts, te);

        /* measure 2 layer memory access loops: case 1 */
        ts = rdtscp(&ui);
        for (j = 0; j < (1UL << 3); j++)
            for (i = 0; i < (1UL << 20); i++)
                tv += *((U64 *)&data[(j + (i << 3)) << 3]);
        te = rdtscp(&ui);
        CALC_MIN(csci[9], ts, te);
    }

    LOGI("2 layer maccess loops (1M * 8): %I64u (%I64u - %I64u)\n",
        (csci[8] <= g_tsc) ? 0 : (csci[8] - g_tsc), csci[8], g_tsc);

    LOGI("2 layer maccess loops (8 * 1M): %I64u (%I64u - %I64u)\n",
        (csci[9] <= g_tsc) ? 0 : (csci[9] - g_tsc), csci[9], g_tsc);

    for (t = 0; t < 50; t++) {

        /* measure 3 layer empty-loops: case 1 */
        ts = rdtscp(&ui);
        for (i = 0; i < (1UL << 20); i++)
            for (m = 0; m < (1UL << 4); m++)
                for (j = 0; j < (1UL << 3); j++);
        te = rdtscp(&ui);
        CALC_MIN(csci[10], ts, te);

        /* measure 3 layer empty-loops: case 2 */
        ts = rdtscp(&ui);
        for (j = 0; j < (1UL << 3); j++)
            for (m = 0; m < (1UL << 4); m++)
                for (i = 0; i < (1UL << 20); i++);
        te = rdtscp(&ui);
        CALC_MIN(csci[11], ts, te);
    }

    LOGI("3 layer empty loops (1M * 64 * 8): %I64u (%I64u - %I64u)\n",
        (csci[10] <= g_tsc) ? 0 : (csci[10] - g_tsc), csci[10], g_tsc);

    LOGI("3 layer empty loops (8 * 64 * 1M): %I64u (%I64u - %I64u)\n",
        (csci[11] <= g_tsc) ? 0 : (csci[11] - g_tsc), csci[11], g_tsc);

    for (t = 0; t < 25; t++) {

        /* measure 3 layer empty-loops: case 1 */
        ts = rdtscp(&ui);
        for (i = 0; i < (1UL << 20); i++)
            for (m = 0; m < (1UL << 4); m++)
                for (j = 0; j < (1UL << 3); j++)
                    tv += *((U64 *)&data[(j + (i << 3)) << 3]);
        te = rdtscp(&ui);
        CALC_MIN(csci[12], ts, te);

        /* measure 3 layer empty-loops: case 2 */
        ts = rdtscp(&ui);
        for (j = 0; j < (1UL << 3); j++)
            for (m = 0; m < (1UL << 4); m++)
                for (i = 0; i < (1UL << 20); i++)
                    tv += *((U64 *)&data[(j + (i << 3)) << 3]);
        te = rdtscp(&ui);
        CALC_MIN(csci[13], ts, te);
    }

    LOGI("3 layer maccess loops (1M * 64 * 8): %I64u (%I64u - %I64u)\n",
        (csci[12] <= g_tsc) ? 0 : (csci[12] - g_tsc), csci[12], g_tsc);

    LOGI("3 layer maccess loops (8 * 64 * 1M): %I64u (%I64u - %I64u)\n",
        (csci[13] <= g_tsc) ? 0 : (csci[13] - g_tsc), csci[13], g_tsc);


    U64 csma[2][4];

    for (j = 0; j < 2; j++)
        for (i = 0; i < 4; i++)
        csma[j][i] = CALC_INITV;

    for (t = 0; t < N_LOOPS; t++) {

        /* flush cache line */
        Sleep(0);
        _mm_clflush(&data[0]);
        for (j = 0; j < 2; j++) {
            /* measure cache miss latency */
            ts = rdtscp(&ui);
            m |= data[0];
            te = rdtscp(&ui);
            CALC_MIN(csma[j][0], ts, te);
        }

        /* flush cache line */
        Sleep(0);
        _mm_clflush(&data[0]);
        for (j = 0; j < 2; j++) {
            /* measure cache miss latency */
            ts = rdtscp(&ui);
            m |= *((USHORT *)&data[0]);
            te = rdtscp(&ui);
            CALC_MIN(csma[j][1], ts, te);
        }

        /* flush cache line */
        Sleep(0);
        _mm_clflush(&data[0]);
        for (j = 0; j < 2; j++) {
            /* measure cache miss latency */
            ts = rdtscp(&ui);
            m |= *((U32*)&data[0]);
            te = rdtscp(&ui);
            CALC_MIN(csma[j][2], ts, te);
        }

        /* flush cache line */
        Sleep(0);
        _mm_clflush(&data[0]);
        for (j = 0; j < 2; j++) {
            /* measure cache miss latency */
            ts = rdtscp(&ui);
            m |= *((U64*)&data[0]);
            te = rdtscp(&ui);
            CALC_MIN(csma[j][3], ts, te);
        }
    }

    LOGI("round, BYTE, SHORT, LONG, LONGLONG\n");
    for (j = 0; j < 2; j++) {
        LOGI("%u, %I64u, %I64u, %I64u, %I64u\n", j, csma[j][0], csma[j][1], csma[j][2], csma[j][3]);
    }
}

/*
 * buffer initialization with pattern
 *
 * bs: block size, start address of each window buffer, default value: 1M bytes aligned
 * ps: buffer offset from g_ptr
 * ws: window buffer size, <= (64M/ns)
 * nw: how many Windows (buffers),  <= ns
 * ss: stride size (1 item per stride), far less than ws
 * pt: memory pattern
       0: forward sequential
       1: backward sequential
       2: randomized
 */
void cl_memset(U32 bs, U32 ps, U32 ws, U32 nw, U32 ss, U32 pt)
{
    ULONG_PTR  *dat = (ULONG_PTR *)(g_ptr + ps);
    U32         i, j;

   if (!bs)
       bs = (ws + 1048576 - 1) & ~(1048576UL - 1);
   if (!ws || !nw || !bs ||         /* invalid values or not */
       (ss & PTR_MASK) ||           /* should be PTR aligned */
       (ws & PTR_MASK) ||
       (ps & PTR_MASK) ||           
       ws > BS_IN_BYTES ||          /* ws < BS_IN_BYTES */
       bs < ws ||
       ps + bs * nw > BS_IN_BYTES + 1048576 ||     /* */
       ws > bs || ss >= ws) {
        exit(0);
    }

    LOGD("\ninit mem: ps: %u ws: %u nw: %u ss: %u, pat: %u.\n", ps, ws, nw, ss, pt);
    for (i = 0; i < ws/ss; i++) {
        LOGD("round: %u:\t [%4I64x] ->", i, (ULONG_PTR)&dat[(i * ss) >> PTR_BITS] - (ULONG_PTR)dat + ps);
        for (j = 0; j < (nw - 1); j++) {
            dat[(i * ss + j * bs) >> PTR_BITS] =
            (ULONG_PTR)&dat[(i * ss + (j + 1) * bs) >> PTR_BITS];
            LOGD("[%8I64x] -> ",  dat[(i * ss + j * bs) >> PTR_BITS] - (ULONG_PTR)dat + ps);
        }
        if (i + 1 < ws/ss)
            dat[(i * ss + j * bs) >> PTR_BITS] = (ULONG_PTR)&dat[(i + 1) * ss >> PTR_BITS];
        else
            dat[(i * ss + j * bs) >> PTR_BITS] = (ULONG_PTR)&dat[0];
        LOGD("[%4I64x]\n", dat[(i * ss + j * bs) >> PTR_BITS] - (ULONG_PTR)dat + ps);
    }
}

#define CLS_BLOCK  (1UL << 15)          /* 32k bytes */
#define CLS_ROUNDS (9 - PTR_BITS)       /* max 512 bytes*/
void cl_measure_cacheline1()
{
    U64 cscl[CLS_ROUNDS];
    register U32 i, j, t, ss, ui = 0;
    register U64 ts, te;

    for (j = 0; j < CLS_ROUNDS; j++)
        cscl[j] = CALC_INITV;

    for (j = 0; j < CLS_ROUNDS; j++) {

        register ULONG_PTR *next;
        ss = 1UL << (PTR_BITS + 1 + j);

        cl_memclr();
        cl_memset(0, 0,              CLS_BLOCK /* ws*/, 1 /*nw*/, ss, 0 /*pat*/);
        cl_memset(0, ss - PTR_BYTES, CLS_BLOCK /* ws*/, 1 /*nw*/, ss, 0 /*pat*/);

        for (t = 0; t < N_LOOPS << 1; t++) {
            for (i = 0; i < (CLS_BLOCK >> PTR_BITS); i++)
                _mm_clflush(&g_ptr[i >> PTR_BITS]);

            /* warn up cache */
            next = (ULONG_PTR *)&g_ptr[0];
            for (i = 0; i < CLS_BLOCK / ss; i++)
                next = (ULONG_PTR *)*next;

            /* measure */
            ts = rdtscp(&ui);
            next = (ULONG_PTR*)&g_ptr[ss - PTR_BYTES];
            for (i = 0; i < CLS_BLOCK / ss; i++)
                next = (ULONG_PTR*)*next;
            te = rdtscp(&ui);
            CALC_MIN(cscl[j], ts, te);
        }
    }

    LOGI("step, latency, clocks, rounds\n");
    for (j = 0; j < CLS_ROUNDS; j++) {
        ss = 1UL << (PTR_BITS + 1 + j);
        LOGI("%u,\t %f, %I64u, %u\n", 1UL << (PTR_BITS + 1 + j),
            1.0 * (cscl[j] - g_tsc) * ss / CLS_BLOCK,
            (cscl[j] - g_tsc), CLS_BLOCK / ss);
    }

    for (j = 1; j < CLS_ROUNDS; j++) {
        if (2.0 * (cscl[j] - g_tsc) > 1.5 * (cscl[j - 1] - g_tsc)) {
            LOGI("cache line size: %u\n", 1UL << (PTR_BITS + j));
            break;
        }
    }
}

#define N_STEPS    (10)
void cl_measure_cacheline2()
{
    register U32 s, i, j, t, mid, ui;
    register U64 ts, te;
    PUCHAR   data = g_ptr;

    /* cache line determination */
    unsigned __int64 cscl[N_STEPS + 1] = { 0 };

    s = 8; /* 8 * 4K = 32k bytes */
    for (j = 0; j <= N_STEPS; j++)
        cscl[j] = CALC_INITV;

    for (j = 0; j < N_STEPS; j++) {
        for (t = 0; t < N_LOOPS; t++) {
            for (i = 0; i <= s << (12 - 3); i++)
                _mm_clflush(&data[i * 8]);

            /* warn up cache */
            for (i = 0; i < (s << (11 - j)); i++)
                mid = data[i << (j + 1)];

            ts = rdtscp(&ui);
            for (i = 0; i < (s << (11 - j)); i++)
                mid &= data[(i << (j + 1)) + (1 << j) - 1];
            te = rdtscp(&ui);
            CALC_MIN(cscl[j], ts, te);
        }
    }

    printf_s("step, latency, delta, rounds\n");
    for (j = 0; j < N_STEPS; j++) {
        printf_s("%u,\t %f, %I64u, %u\n", 1 << j,
            1.0 * (cscl[j] - g_tsc) / (s << (11 - j)), 
            (cscl[j] - g_tsc), s << (11 - j) );
    }
    for (j = 1; j < N_STEPS; j++) {
        if (2.0 * (cscl[j] - g_tsc) > 1.5 * (cscl[j - 1] - g_tsc)) {
            printf_s("cache line size: %u\n", 1 << (j - 1));
            break;
        }
    }
}


#define CHL_ROUNDS   (27 - 10)  /* from 4K to 64M */
#define CHL_HWS      (64)
void cl_measure_cache_hierarchy()
{
    U64 cscl[CHL_ROUNDS], cscb[CHL_ROUNDS];
    register U32 i, j, t, ui = 0, s;
    register U64 ts, te;

    LOGI("\nMeasure cache hierarchy ...\n");

    for (j = 0; j < CHL_ROUNDS; j++)
        cscb[j] = CALC_INITV;

    Sleep(0);
    for (j = 0; j < CHL_ROUNDS; j++) {

        s = (1024UL << j) / CHL_HWS / 2 * 98 / 100 * CHL_HWS * 2;
        for (t = 0; t < N_LOOPS / 2; t++) {
            ts = rdtscp(&ui);
            for (i = 0; i < s / CHL_HWS; i++);
            te = rdtscp(&ui);
            CALC_MIN(cscb[j], ts, te);

        }
    }

    for (j = 0; j < CHL_ROUNDS; j++)
        cscl[j] = CALC_INITV;

    for (j = 0; j < CHL_ROUNDS; j++) {
        register ULONG_PTR *next;

        s = (1024UL << j) / CHL_HWS / 2 * 98 / 100 * CHL_HWS * 2;
        cl_memclr();
        cl_memset(0, 0,                   s /* ws*/, 1 /*nw*/, CHL_HWS, 0 /*pat*/);
        cl_memset(0, CHL_HWS - PTR_BYTES, s /* ws*/, 1 /*nw*/, CHL_HWS, 0 /*pat*/);

        for (t = 0; t < N_LOOPS / 2; t++) {

            for (i = 0; i < (s >> PTR_BITS); i++)
                _mm_clflush(&g_ptr[i >> PTR_BITS]);

            /* warn up cache */
            Sleep(0);
            next = (ULONG_PTR *)&g_ptr[0];
            for (i = 0; i < s / CHL_HWS; i++)
                next = (ULONG_PTR *)*next;

            /* measure */
            next = (ULONG_PTR*)&g_ptr[CHL_HWS - PTR_BYTES];
            ts = rdtscp(&ui);
            for (i = 0; i < s / CHL_HWS; i++)
                next = (ULONG_PTR*)*next;
            te = rdtscp(&ui);
            CALC_MIN(cscl[j], ts, te);

        }
        LOGI("%uK,\t %f, %I64u, %u\n", 1 << j,
            1.0 * (cscl[j] - cscb[j]) * CHL_HWS / s,
            cscl[j] - cscb[j], s / CHL_HWS);
    }

    LOGI("step, latency, clocks, rounds\n");
    for (j = 0; j < CHL_ROUNDS; j++) {
        s = (1024UL << j);
        LOGI("%uK,\t %f, %I64u, %u\n", (s >> 10),
             1.0 * (cscl[j] - cscb[j]) * CHL_HWS / (s - CHL_HWS * 2),
             cscl[j] - cscb[j], s / CHL_HWS);
    }
}

#define CHL_WINDOWS   (14)   /* 1K to 8M */
#define CHL_ASSOWAYS  (64)
void cl_measure_cache_ways()
{
    U64 cscl[CHL_WINDOWS][CHL_ASSOWAYS], cscb[CHL_WINDOWS][CHL_ASSOWAYS];
    register U32 i, j, t, w, ui = 0;
    register U64 ts, te;

    for (w = 0; w < CHL_WINDOWS; w++) {

        for (j = 0; j < CHL_ASSOWAYS; j++)
            cscb[w][j] = CALC_INITV;

        Sleep(0);
        for (j = 0; j < CHL_ASSOWAYS && (j + 1) <= 1UL << (BS_IN_BITS - 10 - w); j++) {
            for (t = 0; t < N_LOOPS; t++) {
                ts = rdtscp(&ui);
                for (i = 0; i < (j + 1) * 16UL; i++);
                te = rdtscp(&ui);
                CALC_MIN(cscb[w][j], ts, te);
            }
        }
    }

    for (w = 0; w < CHL_WINDOWS; w++) {

        LOGI("\nMeasure L1 cache associative ways for window: %u\n", 1UL << (w + 10));
        for (j = 0; j < CHL_ASSOWAYS; j++)
            cscl[w][j] = CALC_INITV;

        for (j = 0; j < CHL_ASSOWAYS && (j + 1) <= 1UL << (BS_IN_BITS - 10 - w); j++) {
            register ULONG_PTR* next;
            cl_memset(1UL << (w + 10), 0,         8UL * CHL_HWS /* ws*/, j + 1 /*nw*/, CHL_HWS, 0 /*pat*/);
            cl_memset(1UL << (w + 10), PTR_BYTES, 8UL * CHL_HWS /* ws*/, j + 1 /*nw*/, CHL_HWS, 0 /*pat*/);
            Sleep(0);

            for (t = 0; t < N_LOOPS; t++) {
                /* warn up cache */
                next = (ULONG_PTR*)&g_ptr[0];
                for (i = 0; i < (j + 1) * 16UL; i++)
                    next = (ULONG_PTR*)*next;

                /* measure */
                next = (ULONG_PTR*)&g_ptr[PTR_BYTES];
                ts = rdtscp(&ui);
                for (i = 0; i < (j + 1) * 16UL; i++)
                    next = (ULONG_PTR*)*next;
                te = rdtscp(&ui);
                CALC_MIN(cscl[w][j], ts, te);
            }
        }
        LOGI("step, latency, clocks, rounds\n");
        for (j = 0; j < CHL_ASSOWAYS && (j + 1) <= 1UL << (BS_IN_BITS - 10 - w); j++) {
            LOGI("%u,\t %.3f, %I64u, %u\n", j + 1,
                1.0 * (cscl[w][j] - cscb[w][j]) / 16UL / (j + 1),
                cscl[w][j] - cscb[w][j], 16UL * (j + 1));
        }
    }

    LOGI("ways");
    for (w = 0; w < CHL_WINDOWS; w++) {
        if ((w + 10) >= 20)
            LOGI(", %uM", 1UL << (w - 10));
        else
            LOGI(", %uK", 1UL << (w + 0));
    }
    LOGI("\n");

    for (j = 0; j < CHL_ASSOWAYS; j++) {
        LOGI("%u", j + 1);
        for (w = 0; w < CHL_WINDOWS && (j + 1) <= 1UL << (BS_IN_BITS - 10 - w); w++)
            LOGI(", %.3f", 1.0 * (cscl[w][j] - cscb[w][j]) / 16UL / (j + 1));
        LOGI("\n");
    }
}

void cl_measure_branchprediction1()
{
    U64 csbp[N_LOOPS/2 + 1] = { 0 };
    register U64 ts, te;
    register U32 j, ui = 0;

    for (j = 0; j <= N_LOOPS/2; j++)
        csbp[j] = CALC_INITV;

    Sleep(0);
    for (j = 0; j <= N_LOOPS/2; j++) {
        ts = rdtscp(&ui);
        if (j & 0x10)
            te = rdtscp(&ui);
        else
            te = rdtscp(&ui);
        CALC_MIN(csbp[j], ts, te);
    }
    LOGI("\nBranch prediction penality: 0 -> %d:\n", N_LOOPS / 2);
    for (j = 0; j <= N_LOOPS/2; j++) {
        LOGI(" %I64u,", csbp[j]);
    }
    LOGI("\n");

    for (j = 0; j <= N_LOOPS / 2; j++)
        csbp[j] = CALC_INITV;

    Sleep(0);
    for (j = 0; j <= N_LOOPS / 2; j++) {
        ts = rdtscp(&ui);
        if ((j + 0x10) & 0x10)
            te = rdtscp(&ui);
        else
            te = rdtscp(&ui);
        CALC_MIN(csbp[j], ts, te);
    }
    LOGI("\nBranch prediction penality: 16 -> %d:\n", 16 + N_LOOPS / 2);
    for (j = 0; j <= N_LOOPS / 2; j++) {
        LOGI(" %I64u,", csbp[j]);
    }
    LOGI("\n");
}

void cl_measure_branchprediction2()
{
    U64 csbp[8];
    U32 *data = (U32 *)g_ptr;
    register U32 i, j, t, s = BS_IN_DWORD / 4, mid, ui;
    register U64 ts, te, sum = 0;
    float    f = 2.56f;

    LOGI("\nBranch prediction penality measurement ...\n");

    for (j = 0; j < 8; j++)
        csbp[j] = CALC_INITV;

    for (i = 0; i < s; i++)
        data[i] = i << 8;

    for (t = 0; t < N_LOOPS; t++) {
        ts = rdtscp(&ui);
        for (i = 0; i < s; i++)
            sum += data[i];
        te = rdtscp(&ui);
        CALC_MIN(csbp[0], ts, te);
    }
    LOGI("only sum:           %10I64u\n", csbp[0] - g_tsc);

    mid = data[s / 2];
    for (t = 0; t < N_LOOPS; t++) {
        ts = rdtscp(&ui);
        for (i = 0; i < s; i++) {
            if (mid > data[i])
                sum += data[i];
        }
        te = rdtscp(&ui);
        CALC_MIN(csbp[1], ts, te);
    }
    LOGI("ordered: cmp + sum: %10I64u\n", csbp[1] - g_tsc);

    for (i = 0; i < s; i++)
        data[i] = rand() * rand();

    mid = data[s / 2];
    for (t = 0; t < N_LOOPS; t++) {
        ts = rdtscp(&ui);
        for (i = 0; i < s; i++) {
            if (mid > data[i])
                sum += data[i];
        }
        te = rdtscp(&ui);
        CALC_MIN(csbp[2], ts, te);
    }
    LOGI("random:  cmp + sum: %10I64u\n", csbp[2] - g_tsc);

    for (i = 0; i < s / 2; i++) {
        data[i * 2 + 0] = BS_IN_DWORD + i + 1;
        data[i * 2 + 1] = BS_IN_DWORD - i - 1;
    }

    mid = BS_IN_DWORD;
    for (t = 0; t < N_LOOPS; t++) {
        ts = rdtscp(&ui);
        for (i = 0; i < s; i++) {
            if (mid > data[i])
                sum += data[i];
        }
        te = rdtscp(&ui);
        CALC_MIN(csbp[3], ts, te);
    }
    LOGI("zigzag 1:cmp + sum: %10I64u\n", csbp[3] - g_tsc);

    for (i = 0; i < s / 4; i++) {
        data[i * 4 + 0] = BS_IN_DWORD + i + 4;
        data[i * 4 + 1] = BS_IN_DWORD + i + 2;
        data[i * 4 + 2] = BS_IN_DWORD - i - 2;
        data[i * 4 + 3] = BS_IN_DWORD - i - 4;
    }

    mid = BS_IN_DWORD;
    for (t = 0; t < N_LOOPS; t++) {
        ts = rdtscp(&ui);
        for (i = 0; i < s; i++) {
            if (mid > data[i])
                sum += data[i];
        }
        te = rdtscp(&ui);
        CALC_MIN(csbp[4], ts, te);
    }
    LOGI("zigzag 2:cmp + sum: %10I64u\n", csbp[4] - g_tsc);

    for (i = 0; i < s / 8; i++) {
        data[i * 8 + 0] = BS_IN_DWORD + i + 1;
        data[i * 8 + 1] = BS_IN_DWORD + i + 2;
        data[i * 8 + 2] = BS_IN_DWORD + i + 3;
        data[i * 8 + 3] = BS_IN_DWORD + i + 4;
        data[i * 8 + 4] = BS_IN_DWORD - i - 1;
        data[i * 8 + 5] = BS_IN_DWORD - i - 2;
        data[i * 8 + 6] = BS_IN_DWORD - i - 3;
        data[i * 8 + 7] = BS_IN_DWORD - i - 4;
    }

    mid = BS_IN_DWORD;
    for (t = 0; t < N_LOOPS; t++) {
        ts = rdtscp(&ui);
        for (i = 0; i < s; i++) {
            if (mid > data[i])
                sum += data[i];
        }
        te = rdtscp(&ui);
        CALC_MIN(csbp[4], ts, te);
    }
    LOGI("zigzag 3:cmp + sum: %10I64u\n", csbp[4] - g_tsc);

    for (i = 0; i < s / 16; i++) {
        data[i * 16 +  0] = BS_IN_DWORD + i + 1;
        data[i * 16 +  1] = BS_IN_DWORD + i + 2;
        data[i * 16 +  2] = BS_IN_DWORD + i + 3;
        data[i * 16 +  3] = BS_IN_DWORD + i + 4;
        data[i * 16 +  4] = BS_IN_DWORD + i + 5;
        data[i * 16 +  5] = BS_IN_DWORD + i + 6;
        data[i * 16 +  6] = BS_IN_DWORD + i + 7;
        data[i * 16 +  7] = BS_IN_DWORD + i + 8;

        data[i * 16 +  8] = BS_IN_DWORD - i - 1;
        data[i * 16 +  9] = BS_IN_DWORD - i - 2;
        data[i * 16 + 10] = BS_IN_DWORD - i - 3;
        data[i * 16 + 11] = BS_IN_DWORD - i - 4;
        data[i * 16 + 12] = BS_IN_DWORD - i - 5;
        data[i * 16 + 13] = BS_IN_DWORD - i - 6;
        data[i * 16 + 14] = BS_IN_DWORD - i - 7;
        data[i * 16 + 15] = BS_IN_DWORD - i - 8;
    }

    mid = BS_IN_DWORD;
    for (t = 0; t < N_LOOPS; t++) {
        ts = rdtscp(&ui);
        for (i = 0; i < s; i++) {
            if (mid > data[i])
                sum += data[i];
        }
        te = rdtscp(&ui);
        CALC_MIN(csbp[5], ts, te);
    }
    LOGI("zigzag 4:cmp + sum: %10I64u\n", csbp[5] - g_tsc);

}


#define N_STEP (4096)
void cl_cache_footprint()
{
    U32      counts[256];
    PUCHAR   data = g_ptr;
    register U32 j, k, t, m, ui;
    register U64 ts = 0, te = 0;


    for (j = 0; j < 16; j++) {

        memset(counts, 0, sizeof(U32) * 256);

        for (t = 0; t < N_LOOPS * 5; t++) {

            /* flush cache line */
            for (m = 0; m < 256; m++)
                _mm_clflush(&data[m << 12]);

            k = data[j << 12];

            /* measure rdtscp latency */
            for (m = 0; m < 256; m++) {
                ts = rdtscp(&ui);
                k = data[m << 12];
                te = rdtscp(&ui);
                if (te - ts < 60)
                    counts[m]++;
            }
        }

        LOGI("%u: ", j);
        for (m = 0; m < 256; m++) {
            if (counts[m] * 100 > N_LOOPS * 5 * 80 ) {
                LOGI(" %X (%u)", m, counts[m]);
            }
        }
        LOGI("\n");
    }      

    return;
}

void cl_fini()
{
    if (g_raw)
        VirtualFree(g_raw, 0, MEM_RELEASE);
}


/* exeution:  start /realtime /affinity 2 CacheLatency.exe */

int main()
{
    /*
     * global initialization ...
     */
    if (cl_init() < 0)
        goto errorout;

    /*
     * measure instructions latency
     */
    cl_measure_ins();

    /*
     * measure cache line size
     */
    cl_measure_cacheline1();
    cl_measure_cacheline2();

    /*
     * measure cache hierarchy
     */
    cl_measure_cache_hierarchy();
    cl_measure_cache_ways();

    /*
     * measure branch prediction penality
     */
    /* case 1: run twice */
    cl_measure_branchprediction1();
    cl_measure_branchprediction1();
    /* case 2: */
    cl_measure_branchprediction2();

errorout:
    system("pause");
    cl_fini();
    return 0; 
}