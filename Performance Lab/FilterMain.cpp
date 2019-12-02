#include <stdio.h>
#include "cs1300bmp.h"
#include <iostream>
#include <fstream>
#include "Filter.h"
#include <omp.h> // this is the library for openMP

using namespace std;

#include "rdtsc.h"

//
// Forward declare the functions
//
Filter * readFilter(string filename);
double applyFilter(Filter *filter, cs1300bmp *input, cs1300bmp *output);

int
main(int argc, char **argv)
{

  if ( argc < 2) {
    fprintf(stderr,"Usage: %s filter inputfile1 inputfile2 .... \n", argv[0]);
  }

  //
  // Convert to C++ strings to simplify manipulation
  //
  string filtername = argv[1];

  //
  // remove any ".filter" in the filtername
  //
  string filterOutputName = filtername;
  string::size_type loc = filterOutputName.find(".filter");
  if (loc != string::npos) {
    //
    // Remove the ".filter" name, which should occur on all the provided filters
    //
    filterOutputName = filtername.substr(0, loc);
  }

  Filter *filter = readFilter(filtername);

  double sum = 0.0;
  int samples = 0;

  for (int inNum = 2; inNum < argc; inNum++) {
    string inputFilename = argv[inNum];
    string outputFilename = "filtered-" + filterOutputName + "-" + inputFilename;
    struct cs1300bmp *input = new struct cs1300bmp;
    struct cs1300bmp *output = new struct cs1300bmp;
    int ok = cs1300bmp_readfile( (char *) inputFilename.c_str(), input);

    if ( ok ) {
      double sample = applyFilter(filter, input, output);
      sum += sample;
      samples++;
      cs1300bmp_writefile((char *) outputFilename.c_str(), output);
    }
    delete input;
    delete output;
  }
  fprintf(stdout, "Average cycles per sample is %f\n", sum / samples);

}

struct Filter *
readFilter(string filename)
{
  ifstream input(filename.c_str());

  if ( ! input.bad() ) {
    int size = 0;
    input >> size;
    Filter *filter = new Filter(size);
    int div;
    input >> div;
    filter -> setDivisor(div);
    for (int i=0; i < size; i++) {
      for (int j=0; j < size; j++) {
	int value;
	input >> value;
	filter -> set(i,j,value);
      }
    }
    return filter;
  } else {
    cerr << "Bad input in readFilter:" << filename << endl;
    exit(-1);
  }
}


double
applyFilter(struct Filter *filter, cs1300bmp *input, cs1300bmp *output)
{

  long long cycStart, cycStop;

  cycStart = rdtscll();

  const short int IMGHeight = input->height;
  const short int IMGWidth = input->width; // removed the pointer and made a vairable to be replaced in the for loop
  const short int traversalHeight = IMGHeight - 1; // these variables are made to replace the traversal method used
  const short int traversalWidth = IMGWidth - 1;
  const short int size = filter -> getSize(); // got rid of the pointer in the loop
  const short int divisor = filter-> getDivisor(); // got rid of this pointer in the loop
  short int j, i, TempRed, TempGreen, TempBlue, getFilter,temp_a, temp_b;
  short int filterArray[size][size];
  output -> width = IMGWidth; // got rid of the input -> height and replaced it with IMGWidth
  output -> height = IMGHeight;

  for(short int a = 0; a < size; ++a){
    for(short int b = 0; b < size; b+=4){
      filterArray[a][b] = filter -> get(a,b);
      filterArray[a][b+1] = filter -> get(a,b+1);
      filterArray[a][b+2] = filter -> get(a,b+2);
      filterArray[a][b+3] = filter -> get(a,b+3);
    }
  }
    
//     #pragma omp parallel
//     #pragma omp for
  for(short int row = 1; row < traversalHeight; ++row){
    for(short int col = 1; col < traversalWidth; ++col){

      TempRed = 0;
      TempGreen = 0;
      TempBlue = 0;

      for(i= 0; i < size; ++i){
        temp_b = row + i - 1; // this is code motion
        for(j = 0; j < size; ++j){
          getFilter = filterArray[i][j];
          temp_a = col + j - 1; // this is code motion
          int short RED = (input -> color[temp_b][temp_a][0] * getFilter);
          int short GREEN = (input -> color[temp_b][temp_a][1] * getFilter);
          int short BLUE = (input -> color[temp_b][temp_a][2] * getFilter);

          TempRed += RED;
          TempGreen += GREEN;
          TempBlue += BLUE;
        }
      }

      TempRed /= divisor;
      TempGreen /= divisor;
      TempBlue /= divisor;

//       if(TempRed < 0){TempRed = 0;}
//       if(TempGreen < 0){TempGreen = 0;}
//       if(TempBlue < 0){TempBlue = 0;}
//       if(TempRed > 255){TempRed = 255;}
//       if(TempGreen > 255){TempGreen = 255;}
//       if(TempBlue > 255){TempBlue = 255;}
        
      TempRed = TempRed < 0 ? 0 : TempRed > 255 ? 255 : TempRed; // this is more efficient than if and else using conditonals  
      TempGreen = TempGreen < 0 ? 0 : TempGreen > 255 ? 255 : TempGreen;
      TempBlue = TempBlue < 0 ? 0 : TempBlue > 255 ? 255 : TempBlue;

      output -> color[row][col][0] = TempRed;
      output -> color[row][col][1] =  TempGreen;
      output -> color[row][col][2] = TempBlue;
    }
  }
  cycStop = rdtscll();
  double diff = cycStop - cycStart;
  double diffPerPixel = diff / (output -> width * output -> height);
  fprintf(stderr, "Took %f cycles to process, or %f cycles per pixel\n",
	  diff, diff / (output -> width * output -> height));
  return diffPerPixel;
}
