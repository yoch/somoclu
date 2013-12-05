/**
 * Self-Organizing Maps on a cluster
 *  Copyright (C) 2013 Peter Wittek
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <getopt.h>

#include "somoclu.h"

using namespace std;

/// For synchronized timing
#ifndef MPI_WTIME_IS_GLOBAL
#define MPI_WTIME_IS_GLOBAL 1
#endif

// Default parameters
#define N_EPOCH 10
#define N_SOM_X 50
#define N_SOM_Y 50
#define KERNEL_TYPE 0
#define MAP_TYPE 0
#define ENABLE_SNAPSHOTS false

void processCommandLine(int argc, char** argv, string *inFilename,
                        string* outPrefix, unsigned int *nEpoch,
                        unsigned int *radius,
                        unsigned int *nSomX, unsigned int *nSomY,
                        unsigned int *kernelType, unsigned int *mapType,
                        bool *enableSnapshots, string *initialCodebookFilename);

/* -------------------------------------------------------------------------- */
int main(int argc, char** argv)
/* -------------------------------------------------------------------------- */
{
    int rank = 0;
    int nProcs = 1;
    
#ifdef HAVE_MPI  
    ///
    /// MPI init
    ///
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nProcs);
    MPI_Barrier(MPI_COMM_WORLD);
#endif

    unsigned int nEpoch = 0;
    unsigned int nSomX = 0;
    unsigned int nSomY = 0;
    unsigned int kernelType = 0;
    unsigned int mapType = 0;
    unsigned int radius = 0;
    bool enableSnapshots = false;
    string inFilename;
    string initialCodebookFilename;
    string outPrefix;

    if (rank==0) {
        processCommandLine(argc, argv, &inFilename, &outPrefix,
                           &nEpoch, &radius, &nSomX, &nSomY,
                           &kernelType, &mapType, &enableSnapshots,
                           &initialCodebookFilename);
#ifndef CUDA
        if (kernelType == DENSE_GPU) {
            cerr << "Somoclu was compile without GPU support!\n";
            my_abort(1);
        }
#endif
    }
#ifdef HAVE_MPI 
    MPI_Bcast(&nEpoch, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&radius, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&nSomX, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&nSomY, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&kernelType, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&mapType, 1, MPI_INT, 0, MPI_COMM_WORLD);
    char *inFilenameCStr = new char[255];
    if (rank == 0) {
        strcpy(inFilenameCStr,inFilename.c_str());
    }
    MPI_Bcast(inFilenameCStr, 255, MPI_CHAR, 0, MPI_COMM_WORLD);
    inFilename = inFilenameCStr;

    double profile_time = MPI_Wtime();
#endif

    float * dataRoot = NULL;
    unsigned int nDimensions = 0;
    unsigned int nVectors = 0;
    if(rank == 0 ) {
        if (kernelType == DENSE_CPU || kernelType == DENSE_GPU) {
            dataRoot = readMatrix(inFilename, nVectors, nDimensions);
        } else {
            readSparseMatrixDimensions(inFilename, nVectors, nDimensions);
        }
    }
#ifdef HAVE_MPI 
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Bcast(&nVectors, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&nDimensions, 1, MPI_INT, 0, MPI_COMM_WORLD);
#endif
    unsigned int nVectorsPerRank = ceil(nVectors / (1.0*nProcs));
    
    // Allocate a buffer on each node
    float* data = NULL;
    svm_node **sparseData;
    sparseData = NULL;

    if (kernelType == DENSE_CPU || kernelType == DENSE_GPU) {
#ifdef HAVE_MPI         
        // Dispatch a portion of the input data to each node
        data = new float[nVectorsPerRank*nDimensions];        
        MPI_Scatter(dataRoot, nVectorsPerRank*nDimensions, MPI_FLOAT,
                    data, nVectorsPerRank*nDimensions, MPI_FLOAT,
                    0, MPI_COMM_WORLD);
#else
        data = dataRoot;
#endif                    
    } else {
        int currentRankProcessed = 0;
        while (currentRankProcessed < nProcs) {
            if (rank == currentRankProcessed) {
                sparseData=readSparseMatrixChunk(inFilename, nVectors, nVectorsPerRank,
                                                 rank*nVectorsPerRank);
            }
            currentRankProcessed++;
#ifdef HAVE_MPI             
            MPI_Barrier(MPI_COMM_WORLD);
#endif            
        }
    }

    if(rank == 0) {
        // No need for root data any more if compiled with MPI
#ifdef HAVE_MPI                 
        if (kernelType == DENSE_CPU || kernelType == DENSE_GPU) {
            delete [] dataRoot;
        }
#endif
        cout << "nVectors: " << nVectors << " ";
        cout << "nVectorsPerRank: " << nVectorsPerRank << " ";
        cout << "nDimensions: " << nDimensions << " ";
        cout << endl;
    }

#ifdef CUDA
    if (kernelType == DENSE_GPU) {
        setDevice(rank, nProcs);
        initializeGpu(data, nVectorsPerRank, nDimensions, nSomX, nSomY);
    }
#endif

#ifdef HAVE_MPI 
    MPI_Barrier(MPI_COMM_WORLD);
#endif
    // TRAINING
    train(rank, data, sparseData, nSomX, nSomY,
          nDimensions, nVectors, nVectorsPerRank,
          nEpoch, radius, outPrefix, enableSnapshots, kernelType, mapType,
          initialCodebookFilename);

    if (kernelType == DENSE_CPU || kernelType == DENSE_GPU) {
        delete [] data;
    } else {
        delete [] sparseData;
    }
#ifdef HAVE_MPI 
    profile_time = MPI_Wtime() - profile_time;
    if (rank == 0) {
        cerr << "Total Execution Time: " << profile_time << endl;
    }
#endif
#ifdef CUDA
    if (kernelType == DENSE_GPU) {
        freeGpu();
    }
#endif
#ifdef HAVE_MPI 
    MPI_Finalize();
#endif
    return 0;
}

void printUsage() {
    cout << "Usage:\n" \
         "     [mpirun -np NPROC] somoclu [OPTIONs] INPUT_FILE OUTPUT_PREFIX\n" \
         "Arguments:\n" \
         "     -c FILENAME           Specify an initial codebook for the map." \
         "     -e NUMBER             Maximum number of epochs (default: " << N_EPOCH << ")\n" \
         "     -k NUMBER             Kernel type (default: " << KERNEL_TYPE << "): \n" \
         "                              0: Dense CPU\n" \
         "                              1: Dense GPU\n" \
         "                              2: Sparse CPU\n" \
         "     -m NUMBER             Map type (default: " << MAP_TYPE << "): \n" \
         "                              0: Planar\n" \
         "                              1: Toroid\n" \
         "     -r NUMBER             Initial radius (default: half of the map in direction x)\n" \
         "     -s                    Enable snapshots of U-matrix (default: false)\n" \
         "     -x, --columns NUMBER  Number of columns in map (size of SOM in direction x) (default: " << N_SOM_X << ")\n" \
         "     -y, --rows NUMBER     Number of rows in map (size of SOM in direction y) (default: " << N_SOM_Y << ")\n" \
         "Examples:\n" \
         "     somoclu data/rgbs.txt data/rgbs\n"
         "     mpirun -np 4 somoclu -k 0 -x 20 -y 20 data/rgbs.txt data/rgbs\n";
}

void processCommandLine(int argc, char** argv, string *inFilename,
                        string* outPrefix, unsigned int *nEpoch,
                        unsigned int *radius,
                        unsigned int *nSomX, unsigned int *nSomY,
                        unsigned int *kernelType, unsigned int *mapType,
                        bool *enableSnapshots, string *initialCodebookFilename) {

    // Setting default values
    *nEpoch = N_EPOCH;
    *nSomX = N_SOM_X;
    *nSomY = N_SOM_Y;
    *kernelType = KERNEL_TYPE;
    *enableSnapshots = ENABLE_SNAPSHOTS;
    *mapType = MAP_TYPE;
    *radius = 0;
    static struct option long_options[] =
    {
        {"rows",  required_argument, 0, 'y'},
        {"columns",    required_argument, 0, 'x'},
        {0, 0, 0, 0}
    };
    int c;
    extern int optind, optopt;
    int option_index = 0;
    while ((c = getopt_long (argc, argv, "hsx:y:e:k:m:r:c:",
                             long_options, &option_index)) != -1) {
        switch (c) {
        case 'c':
            *initialCodebookFilename = optarg;
            break;          
        case 'e':
            *nEpoch = atoi(optarg);
            if (*nEpoch<=0) {
                cerr << "The argument of option -e should be a positive integer.\n";
                my_abort(1);
            }
            break;
        case 'h':
            printUsage();
            my_abort(0);
            break;
        case 'k':
            *kernelType = atoi(optarg);
            if (*kernelType<DENSE_CPU||*kernelType>SPARSE_CPU) {
                cerr << "The argument of option -k should be a valid kernel.\n";
                my_abort(1);
            }
            break;
        case 'm':
            *mapType = atoi(optarg);
            if (*mapType<PLANAR||*mapType>TOROID) {
                cerr << "The argument of option -m should be a valid map type.\n";
                my_abort(1);
            }
            break;
        case 'r':
            *radius = atoi(optarg);
            if (*radius<=0) {
                cerr << "The argument of option -r should be a positive integer.\n";
                my_abort(1);
            }
            break;
        case 's':
            *enableSnapshots = true;
            break;
        case 'x':
            *nSomX = atoi(optarg);
            if (*nSomX<=0) {
                cerr << "The argument of option -x should be a positive integer.\n";
                my_abort(1);
            }
            break;
        case 'y':
            *nSomY = atoi(optarg);
            if (*nSomY<=0) {
                cerr << "The argument of option -y should be a positive integer.\n";
                my_abort(1);
            }
            break;
        case '?':
            if (optopt == 'e' || optopt == 'k' || optopt == 's' ||
                    optopt == 'x'    || optopt == 'y') {
                cerr << "Option -" <<  optopt << " requires an argument.\n";
                printUsage();
                my_abort(1);
            } else if (isprint (optopt)) {
                cerr << "Unknown option `-" << optopt << "'.\n";
                printUsage();
                my_abort(1);
            } else {
                cerr << "Unknown option character `\\x" << optopt << "'.\n";
                printUsage();
                my_abort(1);
            }
        default:
            abort ();
        }
    }
    if (argc-optind!=2) {
        cerr << "Incorrect number of mandatory parameters\n";
        printUsage();
        my_abort(1);
    }
    *inFilename = argv[optind++];
    *outPrefix = argv[optind++];
}

/** Shut down MPI cleanly if something goes wrong
 * @param err - error code to print
 */
void my_abort(int err)
{
    cerr << "Aborted\n";
#ifdef HAVE_MPI    
    my_abort(err);
#else
    exit(err);
#endif
}

/// EOF