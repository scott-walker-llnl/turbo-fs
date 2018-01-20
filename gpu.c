/******************************************************************************
 * FIRESTARTER - A Processor Stress Test Utility
 * Copyright (C) 2017 TU Dresden, Center for Information Services and High
 * Performance Computing
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Contact: daniel.hackenberg@tu-dresden.de
 *****************************************************************************/

/* CUDA error checking based on CudaWrapper.h
 * https://github.com/ashwin/gDel3D/blob/master/GDelFlipping/src/gDel3D/GPU/CudaWrapper.h
 *****************************************************************************/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <errno.h>
#include <sys/types.h>

#include <cuda.h>
#include <cublas_v2.h>
#include <cuda_runtime_api.h>

#include "gpu.h"

#define CUDA_SAFE_CALL( cuerr, dev_index ) cuda_safe_call( cuerr, dev_index, __FILE__, __LINE__ )

static volatile gpustruct_t * gpuvar;

//CUDA Error Checking
static inline void cuda_safe_call( cudaError_t cuerr, int dev_index, const char * file, const int line ) {
    if ( cuerr != cudaSuccess && cuerr != 1)
    {
        fprintf( stderr, "    - CUDA error at %s:%i : error code = %i(%s), device index:%i\n",
                 file, line, cuerr, cudaGetErrorString( cuerr ), dev_index );
        exit(cuerr);
    }

    return;
}

static int roundUp(int numToRound, int multiple) {  
    if(multiple == 0) {  
        return numToRound;
    }  

    int remainder = numToRound % multiple; 
    if (remainder == 0) {
        return numToRound;
    }

    return numToRound + multiple - remainder; 
} 

static int ipow(int base,int exp) {
    int result = 1;
    while (exp) {
        if (exp & 1) result *= base;
        exp >>= 1;
        base *= base;
    }

    return result;
}

#define FILL_SUPPORT(DATATYPE,SIZE,SEED) \
    do { \
        int i; \
        DATATYPE frac; \
        DATATYPE *array= malloc(sizeof(DATATYPE)*SIZE*SIZE); \
        if(!array) { \
            fprintf(stderr, "Could not allocate memory for GPU computation\n"); \
            exit(ENOMEM); \
        } \
        for (i=0; i<SIZE*SIZE; i++) { \
            if(i % 128 == 0) { \
                frac = (DATATYPE) (ipow(*SEED,2) % 131*17) / 10000; \
                array[i]=(DATATYPE) (ipow(*SEED,2) % 131*17) + frac; \
            } \
        } \
        return array; \
    } \
    while( 0 ) \

static void* fillup(int useD, int size, int * s) {
    
    if(useD) {
        FILL_SUPPORT(double, size, s);
    }
    else {
        FILL_SUPPORT(float, size, s);
    }
}

static void* startBurn(void * index) {
    int d_devIndex = *((int*)index);   //GPU Index. Used to pin this pthread to the GPU.
    int d_iters,i;
    int pthread_useDouble = gpuvar->useDouble; //local per-thread variable, if there's a GPU in the system without Double Precision support.
    int size_use=0;
    if (gpuvar->msize>0){
        size_use=gpuvar->msize;
    }
    void *A = NULL;
    void *B = NULL;
    int seed = 123;     //seed for pseudo random
    CUcontext d_ctx;
    size_t useBytes, d_resultSize;
    struct cudaDeviceProp properties;

    //Reserving the GPU and Initializing cublas
    CUdevice d_dev;
    cublasHandle_t d_cublas;
    CUDA_SAFE_CALL(cuDeviceGet(&d_dev, d_devIndex), d_devIndex);
    CUDA_SAFE_CALL(cuCtxCreate(&d_ctx, 0, d_dev), d_devIndex);
    CUDA_SAFE_CALL(cuCtxSetCurrent(d_ctx), d_devIndex);
    CUDA_SAFE_CALL(cublasCreate(&d_cublas), d_devIndex);
    CUDA_SAFE_CALL(cudaGetDeviceProperties(&properties,d_devIndex), d_devIndex);

    //getting Informations about the GPU Memory
    size_t availMemory, totalMemory;
    CUDA_SAFE_CALL(cuMemGetInfo(&availMemory,&totalMemory), d_devIndex);

    //Defining Memory Pointers
    CUdeviceptr d_Adata;
    CUdeviceptr d_Bdata;
    CUdeviceptr d_Cdata;

    //we check for double precision support on the GPU and print errormsg, when the user wants to compute DP on a SP-only-Card.
    if(pthread_useDouble && properties.major<=1 && properties.minor<=2) {
        fprintf(stderr,"    - GPU %d: %s Doesn't support double precision.\n",d_devIndex,properties.name);
        fprintf(stderr,"    - GPU %d: %s Compute Capability: %d.%d. Requiered for double precision: >=1.3\n",d_devIndex,properties.name,properties.major,properties.minor);
        fprintf(stderr,"    - GPU %d: %s Stressing with single precision instead. Maybe use -f parameter.\n",d_devIndex,properties.name);
        pthread_useDouble=0;
    }

    //check if the user has not set a matrix OR has set a too big matrixsite and if this is true: set a good matrixsize
    if( !size_use || ( ( size_use * size_use * pthread_useDouble?sizeof(double):sizeof(float) * 3 > availMemory ) ) ) {
        size_use=roundUp((int)(0.8*sqrt(((availMemory)/((pthread_useDouble?sizeof(double):sizeof(float))*3)))),1024); //a multiple of 1024 works always well
    }
    if( pthread_useDouble ) {
        useBytes = (size_t)((double)availMemory);
        d_resultSize = sizeof(double)*size_use*size_use;
    }
    else {
        useBytes = (size_t)((float)availMemory);
        d_resultSize = sizeof(float)*size_use*size_use;
    }
    d_iters = (useBytes - 2*d_resultSize)/d_resultSize; // = 1;
    
    //Allocating memory on the GPU
    A=fillup(pthread_useDouble,size_use, &seed);
    B=fillup(pthread_useDouble,size_use, &seed);
    CUDA_SAFE_CALL(cuMemAlloc(&d_Adata, d_resultSize), d_devIndex);
    CUDA_SAFE_CALL(cuMemAlloc(&d_Bdata, d_resultSize), d_devIndex);
    CUDA_SAFE_CALL(cuMemAlloc(&d_Cdata, d_iters*d_resultSize), d_devIndex);
    
    // Moving matrices A and B to the GPU
    CUDA_SAFE_CALL(cuMemcpyHtoD_v2(d_Adata, A, d_resultSize), d_devIndex);
    CUDA_SAFE_CALL(cuMemcpyHtoD_v2(d_Bdata, B, d_resultSize), d_devIndex);

    //initialize d_Cdata with copies of A
    for (i = 0; i < d_iters; i++ ) {
        CUDA_SAFE_CALL(cuMemcpyHtoD_v2(d_Cdata + i*size_use*size_use, A, d_resultSize), d_devIndex);
    }
    
    const float alpha = 1.0f;
    const float beta = 0.0f;
    const double alphaD = 1.0;
    const double betaD = 0.0;

    if(gpuvar->verbose) {
        printf("    - GPU %d: %s Initialized with %lu MB of memory (%lu MB available, using %lu MB of it) and Matrix Size: %d.\n",d_devIndex,properties.name,totalMemory/1024ul/1024ul, availMemory/1024ul/1024ul, useBytes/1024/1024,size_use);
    }
    
    //Actual stress begins here, we set the loadingdone variable on true so that the CPU workerthreads can start too. But only gputhread #0 is setting the variable, to prohibite race-conditioning...
    if(d_devIndex==0) {
        gpuvar->loadingdone=1;
    }
    for(;;) {
        for (i = 0; i < d_iters; i++) {
            if(pthread_useDouble) {
                CUDA_SAFE_CALL(cublasDgemm(d_cublas, CUBLAS_OP_N, CUBLAS_OP_N,
                                         size_use, size_use, size_use, &alphaD,
                                         (const double*)d_Adata, size_use,
                                         (const double*)d_Bdata, size_use,
                                         &betaD,
                                         (double*)d_Cdata + i*size_use*size_use, size_use), d_devIndex);
            }
            else {
                CUDA_SAFE_CALL(cublasSgemm(d_cublas, CUBLAS_OP_N, CUBLAS_OP_N,
                                         size_use, size_use, size_use, &alpha,
                                         (const float*)d_Adata, size_use,
                                         (const float*)d_Bdata, size_use,
                                         &beta,
                                         (float*)d_Cdata + i*size_use*size_use, size_use), d_devIndex);
            }
        }
    }

    CUDA_SAFE_CALL(cuMemFree_v2(d_Adata), d_devIndex);
    CUDA_SAFE_CALL(cuMemFree_v2(d_Bdata), d_devIndex);
    CUDA_SAFE_CALL(cuMemFree_v2(d_Cdata), d_devIndex);
    CUDA_SAFE_CALL(cublasDestroy(d_cublas), d_devIndex);
    free(A);
    free(B);

    return NULL;
}

void* initgpu(void * gpu) {
    gpuvar = (gpustruct_t*)gpu;

    if(gpuvar->useDevice) {
        CUDA_SAFE_CALL(cuInit(0), -1);
        int devCount;
        CUDA_SAFE_CALL(cuDeviceGetCount(&devCount), -1);
        if (devCount) {
            int *dev=malloc(sizeof(int)*devCount);;
            pthread_t gputhreads[devCount]; //creating as many threads as GPUs in the System.
            if(gpuvar->verbose) printf("\n  graphics processor characteristics:\n");
            int i;
            if( gpuvar->useDevice==-1 ) { //use all GPUs if the user gave no information about useDevice 
                gpuvar->useDevice=devCount;
            }
            if ( gpuvar->useDevice > devCount ) {
                printf("    - You requested more CUDA devices than available. Maybe you set CUDA_VISIBLE_DEVICES?\n");
                printf("    - FIRESTARTER will use %d of the requested %d CUDA device(s)\n",devCount,gpuvar->useDevice);
                gpuvar->useDevice=devCount;
            }

            for(i=0; i<gpuvar->useDevice; ++i) {
                dev[i]=i; //creating seperate ints, so no race-condition happens when pthread_create submits the adress
                pthread_create(&gputhreads[i],NULL,startBurn,(void *)&(dev[i]));
            }

            /* join computation threads */
            for(i=0; i<gpuvar->useDevice; ++i) {
                pthread_join(gputhreads[i],NULL);
            }

            free(dev);
        }
        else {
            if(gpuvar->verbose) {
                printf("    - No CUDA devices. Just stressing CPU(s). Maybe use FIRESTARTER instead of FIRESTARTER_CUDA?\n");
            }
            gpuvar->loadingdone=1;
        }
    }
    else {
        if(gpuvar->verbose) {
            printf("    --gpus 0 is set. Just stressing CPU(s). Maybe use FIRESTARTER instead of FIRESTARTER_CUDA?\n");
        }
        gpuvar->loadingdone=1;
    }

    return NULL;
}