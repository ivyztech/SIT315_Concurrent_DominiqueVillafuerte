#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <random>

using namespace std;

int main(int argc, char** argv){
    if(argc<6){
        cerr << "Usage: ./gen <hours> <lights> <stepMin> <seed> <out.csv>\n";
        return 1;
    }
    int hours = stoi(argv[1]);
    int L = stoi(argv[2]);
    int step = stoi(argv[3]); // e.g., 5
    int seed = stoi(argv[4]);
    string out = argv[5];

    mt19937 rng(seed);
    uniform_int_distribution<int> base(0, 10), spike(0, 100);

    ofstream f(out);
    if(!f){ cerr << "Cannot open " << out << "\n"; return 1; }

    int idx=0; // 5-minute slot index
    for(int h=0; h<hours; ++h){
        for(int m=0; m<60; m+=step){
            for(int li=0; li<L; ++li){
                int cars = base(rng);
                if(uniform_int_distribution<int>(0,20)(rng)==0) cars += spike(rng);
                f << idx << ",L" << setw(3) << setfill('0') << li << "," << cars << "\n";
            }
            ++idx;
        }
    }
    return 0;
}
