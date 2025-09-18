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

using namespace std;

struct Record {
    uint64_t minuteIdx;
    string light;
    int cars;
};

static inline int parseLightIdx(const string& s){
    if(s.size()>=2 && (s[0]=='L' || s[0]=='l')){
        int v=0; for(size_t i=1;i<s.size();++i){ if(isdigit((unsigned char)s[i])) v = v*10 + (s[i]-'0'); }
        return v;
    }
    // Fallback: hash map if needed (kept local here)
    static unordered_map<string,int> mapv; static int nextId=0;
    auto it = mapv.find(s);
    if(it!=mapv.end()) return it->second;
    return mapv[s]=nextId++;
}

static inline long long hourFromSlot(long long minuteIdx, int stepMin){
    // stepMin assumed = 5 for compatibility with dataset/commands
    return (minuteIdx * stepMin) / 60;
}

int main(int argc, char** argv){
    if(argc < 3){
        cerr << "Usage: ./seq <input.csv> <topN>\n";
        return 1;
    }
    string path = argv[1];
    int topN = stoi(argv[2]);
    const int stepMin = 5; // matches generator defaults and assignment runs

    ifstream in(path);
    if(!in){ cerr << "Cannot open " << path << "\n"; return 1; }

    // hour -> (lightIdx -> sum)
    unordered_map<long long, unordered_map<int,long long>> totals;

    string line; long long skipped=0;
    while(getline(in, line)){
        if(line.empty()) continue;
        stringstream ss(line);
        string a,b,c;
        if(!getline(ss, a, ',')) { skipped++; continue; }
        if(!getline(ss, b, ',')) { skipped++; continue; }
        if(!getline(ss, c, ',')) { skipped++; continue; }
        try{
            long long minuteIdx = stoll(a);
            int lightIdx = parseLightIdx(b);
            int cars = stoi(c);
            long long h = hourFromSlot(minuteIdx, stepMin);
            totals[h][lightIdx] += cars;
        }catch(...){ skipped++; }
    }

    // Deterministic printing
    vector<long long> hours;
    hours.reserve(totals.size());
    for(auto& kv: totals) hours.push_back(kv.first);
    sort(hours.begin(), hours.end());

    for(long long h: hours){
        auto& mp = totals[h];
        vector<pair<long long,int>> v; v.reserve(mp.size()); // (sum, light)
        for(auto& kv: mp) v.push_back({kv.second, kv.first});
        sort(v.begin(), v.end(), [](auto& A, auto& B){
            if(A.first!=B.first) return A.first>B.first;
            return A.second<B.second;
        });
        cout << "Hour " << h << " top " << topN << ":\n";
        for(int i=0;i<topN && i<(int)v.size();++i){
            cout << "  L" << setw(3) << setfill('0') << v[i].second << " -> " << v[i].first << "\n";
        }
    }
    if(skipped>0) cerr << "[seq] skipped=" << skipped << " malformed lines\n";
    return 0;
}
