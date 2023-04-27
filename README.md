# TAGE_CBP

There is another README file in this repo which contains the original competition instructions that I used to integrate the predictors and test them.
This current README file has instructions for the user to run the predictors already integrated to get the results in the form:

1000*wrong_cc_predicts/total insts: 1000 *    52477 / 29499988 =   1.779
total branches:                   2616522
total cc branches:                2213673
total predicts:                   2213673
*********************************************************

The user can change the file "tread.cc" if they want to print more/ different statistics.

##Instructions

The files for predictors are:
   predictor_tage.cc, predictor_tage.h, main_org.cc - TAGE branch predictor
   predictor_gem5.cc, predictor_gem5.cc, main_gem5.cc - TAGE branch predictor as implemented in gem5 which I made compatible with and integrated in CBP 
   predictor_demo.cc, predictor_demo.h, main_org.cc - Gshare branch predictor used in CBP as demo

The traces are in the directory called "traces"

1. The files used to create the executable are predictor.cc, predictor.h and main.cc
2. So, in order to run a type of predictor, copy the respective predictor_<>.cc, predictor_<>.h and main_<> to predictor.cc, predictor.h and main.cc.
3. Then type - make to create the executables.
4. Then run the executable using - ./predictor traces/without-values/<>
5. The result will be displayed on the prompt.

