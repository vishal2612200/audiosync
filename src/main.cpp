// AudioSync is the audio synchronization feature made for spotivids. It is
// inspired by Allison Deal's VideoSync, but much faster.
// (https://github.com/allisonnicoledeal/VideoSync)
//
// This module is in C++ mainly because of speed. FFTW is the fastest Fourier
// Transformation library. Python threading can also be not real concurrency
// because of the GIL.
//
// The objective of the module is to obtain the delay between two audio files.
// In its usage, one of them will be the YouTube downloaded video and another
// the recorded song.

#include <iostream>
#include <vector>
#include <math.h>
#include <mutex>
#include <thread>
#include <fftw3.h>
#include "../include/AudioFile.h"

#ifdef DEBUG
// Plotting output in debug mode.
#include "../include/matplotlibcpp.h"
namespace plt = matplotlibcpp;
#endif

#define REAL 0
#define IMAG 1


// Global mutex used in multithreading.
std::mutex GLOBAL_MUTEX;


// Calculating the magnitude of a complex number.
inline double getMagnitude(double r, double i) {
    return sqrt(r*r + i*i);
}


// Concurrent implementation of the Fast Fourier Transform using FFTW.
void fft(std::vector<double> &in, fftw_complex *out) {
    fftw_plan p;
    { std::unique_lock<std::mutex> lock(GLOBAL_MUTEX);
        p = fftw_plan_dft_r2c_1d(in.size(), &in[0], out, FFTW_ESTIMATE);
    }

    fftw_execute(p);

    { std::unique_lock<std::mutex> lock(GLOBAL_MUTEX);
        fftw_destroy_plan(p);
    }
}

// Concurrent implementation of the Inverse Fast Fourier Transform using FFTW.
void ifft(fftw_complex *in, double out[], size_t length) {
    fftw_plan p;
    { std::unique_lock<std::mutex> lock(GLOBAL_MUTEX);
        p = fftw_plan_dft_c2r_1d(length, in, out, FFTW_ESTIMATE);
    }

    fftw_execute(p);

    { std::unique_lock<std::mutex> lock(GLOBAL_MUTEX);
        fftw_destroy_plan(p);
    }
}


// Calculating the cross-correlation between two signals `a` and `b`:
//     xcross = ifft(fft(a) * magn(fft(b)))
// And where magn() is the magnitude of the complex numbers returned by the
// fft. Instead of magn(), a reverse() function could be used. But the
// magnitude seems easier to calculate for now.
//
// Both datasets should have the same size. They should be zero-padded to
// length 2N-1. This is needed to calculate the circular cross-correlation
// rather than the regular cross-correlation.
//
// TODO: Check NULL malloc and etc.
// TODO: Get confidence level https://dsp.stackexchange.com/questions/9797/cross-correlation-peak
//
// Returns the delay in milliseconds the second data set has over the first
// one, with a confidence between 0 and 1.
double crossCorrelation(std::vector<double> &data1, std::vector<double> &data2, size_t length, double &confidence) {
    // Getting the complex results from both FFT. The output length for the
    // complex numbers is n/2+1.
    fftw_complex *out1;
    out1 = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * ((length/2)+1));
    fftw_complex *out2;
    out2 = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * ((length/2)+1));

    std::thread fft1(&fft, std::ref(data1), std::ref(out1));
    std::thread fft2(&fft, std::ref(data2), std::ref(out2));
    fft1.join();
    fft2.join();

    // Product of fft1 * mag(fft2) where fft1 is complex and mag(fft2) is real.
    double magnitude;
    fftw_complex *in;
    in = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * length);
    for (size_t i = 0; i < (length/2)+1; ++i) {
        magnitude = getMagnitude(out2[i][REAL], out2[i][IMAG]);
        in[i][REAL] = out1[i][REAL] * magnitude;
        in[i][IMAG] = out1[i][IMAG] * magnitude;
    }
    double *results;
    results = (double *) fftw_malloc(sizeof(fftw_complex) * length);
    ifft(in, results, length);
    confidence = results[0];
    int delay = 0;
    for (size_t i = 1; i < length; ++i) {
        if (abs(results[i]) > confidence) {
            confidence = results[i];
            // std::cout << results[i] << " ";
            delay = i;
        }
    }

    std::cout << "Finished with confidence " << confidence << " and delay " << delay << std::endl;

#ifdef DEBUG
    plt::plot(std::vector<double>(results, results + length));
    plt::show();
    std::cout << (delay * (1.0/48000.0)) << "s of delay" << std::endl;
#endif

    free(out1);
    free(out2);
    free(in);
    free(results);

    // Conversion to milliseconds with 48,000KHz simplified
    return delay / 48.0;
}


// Function to concurrently open and process both files. Takes the name of
// the file to process, and returns a vector with the normalized maximum
// frequency throughout the track.
void runProcessing(std::string name, std::vector<double> &out) {
    // Loading the WAV file
    AudioFile<double> audio;
    audio.load(name);

    // Checking that the file was read correctly
    if (audio.getNumSamplesPerChannel() == 0) {
        throw std::runtime_error("Could not read " + name);
    }

    // Only using 5 seconds (audio is 48,000KHz)
    // Half the vector has to be of size 2N-1, the rest being filled with zeroes.
    out = audio.samples[0];
    int n = 48000 * 5 * 2;
    out.resize(n);
    for (int i = n/2; i < n; ++i)
        out.push_back(0);
}


int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <file1>.wav <file2>.wav\n";
        exit(1);
    }

#ifdef DEBUG
    // Timers in debug mode
    std::clock_t start = std::clock();
#endif

    // Processing both files with multi-threading.
    std::vector<double> out1;
    std::vector<double> out2;
    std::thread t1(&runProcessing, argv[1], std::ref(out1));
    std::thread t2(&runProcessing, argv[2], std::ref(out2));
    t1.join();
    t2.join();

#ifdef DEBUG
    double duration = (std::clock() - start) / (double) CLOCKS_PER_SEC;
    std::cout << "Loading WAVs took " + std::to_string(duration) + "s \n";

    // Benchmarking the matchinv function
    start = std::clock();
    std::vector<double> old1(out1);
    std::vector<double> old2(out2);
#endif

    double confidence;
    double delay = crossCorrelation(out1, out2, out1.size(), confidence);

#ifdef DEBUG
    duration = (std::clock() - start) / (double) CLOCKS_PER_SEC;
    std::cout << "Matching took " + std::to_string(duration) + "s \n";;
    double samplesDelay = delay * 48;
    if (samplesDelay < 0) {
        old1.erase(old1.begin(), old1.size() > samplesDelay ?  old1.begin() + samplesDelay : old1.end());
    } else {
        old2.erase(old2.begin(), old2.size() > samplesDelay ?  old2.begin() + samplesDelay : old2.end());
    }
    plt::plot(old1);
    plt::plot(old2);
    plt::show();
#endif

    return 0;
}