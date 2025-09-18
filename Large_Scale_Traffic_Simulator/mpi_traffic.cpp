#include <mpi.h>

#include <iostream>
#include <fstream>       
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <unordered_map>  
#include <stdexcept>
#include <cstring>      
#include <cctype>        


using namespace std;

// Protocol tags 
const int TAG_WORK  = 1;
const int TAG_STOP  = 2;
const int TAG_READY = 3;

struct Rec { int minuteIdx; int lightIdx; int cars; }; // 3 * int contiguous
static_assert(sizeof(Rec) == 3*sizeof(int), "Rec must be trivially contiguous ints");

static inline int parseLightIdx(const string& s){
    if(!s.empty() && (s[0]=='L'||s[0]=='l')){
        int v=0; for(size_t i=1;i<s.size();++i){ if(isdigit((unsigned char)s[i])) v = v*10 + (s[i]-'0'); }
        return v;
    }
    static unordered_map<string,int> mapv; static int nextId=0;
    auto it = mapv.find(s); if(it!=mapv.end()) return it->second;
    return mapv[s]=nextId++;
}

static inline long long hourFromSlot(long long minuteIdx, int stepMin){
    return (minuteIdx * stepMin) / 60;
}

// Parse once to discover H (hours) and L (lights)
static void discover_dims(const string& path, int stepMin, int& H_out, int& L_out, long long& skipped){
    ifstream in(path);
    if(!in) throw runtime_error("Cannot open " + path);
    long long maxMinute = 0; int maxLight = 0;
    string line; skipped = 0;
    while(getline(in, line)){
        if(line.empty()) continue;
        string a,b,c; stringstream ss(line);
        if(!getline(ss,a,',')){ skipped++; continue; }
        if(!getline(ss,b,',')){ skipped++; continue; }
        if(!getline(ss,c,',')){ skipped++; continue; }
        try{
            long long m = stoll(a);
            int Lidx = parseLightIdx(b);
            (void)stoi(c); // validate parse
            if(m > maxMinute) maxMinute = m;
            if(Lidx > maxLight) maxLight = Lidx;
        }catch(...){ skipped++; }
    }
    H_out = (int)hourFromSlot(maxMinute, stepMin) + 1; // inclusive buckets
    L_out = maxLight + 1;                              // 0..maxLight
    if(H_out<=0 || L_out<=0) throw runtime_error("Invalid dimensions discovered");
}

// Read whole file into Rec vector while skipping bad lines & count
static vector<Rec> read_all_recs(const string& path, int stepMin, long long& skipped){
    ifstream in(path);
    if(!in) throw runtime_error("Cannot open " + path);
    vector<Rec> v; v.reserve(1<<20);
    string line; skipped = 0;
    while(getline(in, line)){
        if(line.empty()) continue;
        string a,b,c; stringstream ss(line);
        if(!getline(ss,a,',')){ skipped++; continue; }
        if(!getline(ss,b,',')){ skipped++; continue; }
        if(!getline(ss,c,',')){ skipped++; continue; }
        try{
            long long m = stoll(a);
            int Lidx = parseLightIdx(b);
            int cars = stoi(c);
            v.push_back(Rec{(int)m, Lidx, cars});
        }catch(...){ skipped++; }
    }
    return v;
}

static void compute_topN_and_print(const vector<long long>& globalTotals, int H, int L, int topN){
    // For each hour, gather pairs (sum, lightIdx), sort deterministically, print
    for(int h=0; h<H; ++h){
        vector<pair<long long,int>> v; v.reserve(L);
        const long long* row = &globalTotals[(size_t)h * L];
        for(int l=0; l<L; ++l){
            long long s = row[l];
            if(s!=0) v.push_back({s, l});
        }
        sort(v.begin(), v.end(), [](auto& A, auto& B){
            if(A.first!=B.first) return A.first>B.first;
            return A.second<B.second;
        });
        cout << "Hour " << h << " top " << topN << ":\n";
        for(int i=0;i<topN && i<(int)v.size(); ++i){
            cout << "  L" << setw(3) << setfill('0') << v[i].second << " -> " << v[i].first << "\n";
        }
    }
}

// Master (blocking) 
static void master_blocking(const vector<Rec>& recs, int H, int L, int stepMin,
                            int topN, int batchSize, int world) {
    const int workers = world - 1;
    if (workers <= 0) { cerr << "No workers.\n"; return; }

    const int total = (int)recs.size();
    int next = 0;

    vector<char> stopped(world, 0);
    int activeWorkers = workers;

    // 1) Collect initial READY from each worker (they send one on startup)
    for (int r = 1; r < world; ++r) {
        int dummy;
        MPI_Recv(&dummy, 1, MPI_INT, r, TAG_READY, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    // 2) PRE-DISPATCH: send one batch to every worker now
    for (int r = 1; r < world; ++r) {
        if (next < total) {
            int count = min(batchSize, total - next);
            vector<int> buf(count * 3);
            for (int i = 0; i < count; ++i) {
                const Rec& rec = recs[next + i];
                buf[i*3 + 0] = rec.minuteIdx;
                buf[i*3 + 1] = rec.lightIdx;
                buf[i*3 + 2] = rec.cars;
            }
            MPI_Send(buf.data(), (int)buf.size(), MPI_INT, r, TAG_WORK, MPI_COMM_WORLD);
            next += count;
        } else {
            MPI_Send(nullptr, 0, MPI_INT, r, TAG_STOP, MPI_COMM_WORLD);
            stopped[r] = 1;
            --activeWorkers;
        }
    }

    // 3) Steady-state: on each READY, send next WORK or STOP
    while (activeWorkers > 0) {
        MPI_Status st;
        int dummy;
        MPI_Recv(&dummy, 1, MPI_INT, MPI_ANY_SOURCE, TAG_READY, MPI_COMM_WORLD, &st);
        int r = st.MPI_SOURCE;

        if (next < total) {
            int count = min(batchSize, total - next);
            vector<int> buf(count * 3);
            for (int i = 0; i < count; ++i) {
                const Rec& rec = recs[next + i];
                buf[i*3 + 0] = rec.minuteIdx;
                buf[i*3 + 1] = rec.lightIdx;
                buf[i*3 + 2] = rec.cars;
            }
            MPI_Send(buf.data(), (int)buf.size(), MPI_INT, r, TAG_WORK, MPI_COMM_WORLD);
            next += count;
        } else if (!stopped[r]) {
            MPI_Send(nullptr, 0, MPI_INT, r, TAG_STOP, MPI_COMM_WORLD);
            stopped[r] = 1;
            --activeWorkers;
        }
    }

    // 4) Global reduction and print
    vector<long long> globalTotals((size_t)H * L, 0);
    vector<long long> localZero((size_t)H * L, 0);
    MPI_Reduce(localZero.data(), globalTotals.data(), H*L, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

    compute_topN_and_print(globalTotals, H, L, topN);
}


//  Master (non-blocking --async)
struct PendingSend {
    int dest;
    vector<int> buf;      // holds data while request in flight
    MPI_Request req{MPI_REQUEST_NULL};
    int tag{TAG_WORK};
};

static void master_async(const vector<Rec>& recs, int H, int L, int /*stepMin*/,
                         int topN, int batchSize, int world) {
    const int workers = world - 1;
    if (workers <= 0) { cerr << "No workers.\n"; return; }

    // Post one READY Irecv per worker (token "I'm idle, send me work")
    vector<int> readyBuf(workers, 0);
    vector<MPI_Request> readyReq(workers, MPI_REQUEST_NULL);
    for (int i = 0; i < workers; ++i) {
        int r = i + 1;
        MPI_Irecv(&readyBuf[i], 1, MPI_INT, r, TAG_READY, MPI_COMM_WORLD, &readyReq[i]);
    }

    const int total = (int)recs.size();
    int next = 0;
    int stoppedCount = 0;
    vector<char> stopped(world, 0);

    struct PendingSend {
        int dest;
        vector<int> buf;              // holds payload until Isend completes
        MPI_Request req{MPI_REQUEST_NULL};
        int tag{TAG_WORK};
    };
    vector<PendingSend> sends; sends.reserve(workers * 2);

    auto send_batch = [&](int r) -> bool {
        if (next >= total) return false;
        const int count = min(batchSize, total - next);
        PendingSend ps;
        ps.dest = r;
        ps.buf.resize(count * 3);
        for (int i = 0; i < count; ++i) {
            const Rec& rec = recs[next + i];
            ps.buf[i*3+0] = rec.minuteIdx;
            ps.buf[i*3+1] = rec.lightIdx;
            ps.buf[i*3+2] = rec.cars;
        }
        MPI_Isend(ps.buf.data(), (int)ps.buf.size(), MPI_INT, r, TAG_WORK,
                  MPI_COMM_WORLD, &ps.req);
        sends.push_back(std::move(ps));
        next += count;
        return true;
    };

    auto send_stop = [&](int r) {
        if (stopped[r]) return;
        PendingSend ps;
        ps.dest = r; ps.tag = TAG_STOP;
        MPI_Isend(nullptr, 0, MPI_INT, r, TAG_STOP, MPI_COMM_WORLD, &ps.req);
        sends.push_back(std::move(ps));
        stopped[r] = 1; ++stoppedCount;
    };

    // Main loop
    while (stoppedCount < workers) {
        int idx;
        MPI_Waitany(workers, readyReq.data(), &idx, MPI_STATUS_IGNORE);
        if (idx == MPI_UNDEFINED) break;        
        const int r = idx + 1;                 


        if (send_batch(r)) {
            MPI_Irecv(&readyBuf[idx], 1, MPI_INT, r, TAG_READY, MPI_COMM_WORLD, &readyReq[idx]);
        } else {
            send_stop(r);
            // mark this slot as no longer expecting READY from r
            readyReq[idx] = MPI_REQUEST_NULL;
        }

        // Reap completed Isends (WORK/STOP) safely
        for (size_t i = 0; i < sends.size(); ) {
            int done = 0;
            MPI_Test(&sends[i].req, &done, MPI_STATUS_IGNORE);
            if (done) sends.erase(sends.begin() + i);
            else ++i;
        }
    }

    // Ensure all outstanding sends complete (flush network buffers)
    for (auto& s : sends) {
        if (s.req != MPI_REQUEST_NULL) MPI_Wait(&s.req, MPI_STATUS_IGNORE);
    }

    // Cancel+wait any still-posted READY Irecvs so no requests linger at Finalize
    for (int i = 0; i < workers; ++i) {
        if (readyReq[i] != MPI_REQUEST_NULL) {
            int done = 0;
            MPI_Test(&readyReq[i], &done, MPI_STATUS_IGNORE);
            if (!done) MPI_Cancel(&readyReq[i]);
            MPI_Wait(&readyReq[i], MPI_STATUS_IGNORE);
        }
    }

    // Global reduction (master contributes zeros) and deterministic print
    vector<long long> globalTotals((size_t)H * L, 0);
    vector<long long> localZero((size_t)H * L, 0);
    MPI_Reduce(localZero.data(), globalTotals.data(), H*L, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    compute_topN_and_print(globalTotals, H, L, topN);
}



//  Worker 
static void worker_loop(int rank, int H, int L, int stepMin){
    vector<long long> local((size_t)H * L, 0);

    // Announce READY on startup
    int token = 1;
    MPI_Send(&token, 1, MPI_INT, 0, TAG_READY, MPI_COMM_WORLD);

    while(true){
        // Probe to see what's next (WORK or STOP)
        MPI_Status st;
        MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
        if(st.MPI_TAG == TAG_STOP){
            MPI_Recv(nullptr, 0, MPI_INT, 0, TAG_STOP, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            break;
        }else if(st.MPI_TAG == TAG_WORK){
            int countInts=0;
            MPI_Get_count(&st, MPI_INT, &countInts);
            if(countInts % 3 != 0){
                // drain and ignore malformed
                vector<int> drain(countInts);
                MPI_Recv(drain.data(), countInts, MPI_INT, 0, TAG_WORK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }else{
                int triples = countInts / 3;
                vector<int> buf(countInts);
                MPI_Recv(buf.data(), countInts, MPI_INT, 0, TAG_WORK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for(int i=0;i<triples;++i){
                    int minuteIdx = buf[i*3+0];
                    int lightIdx  = buf[i*3+1];
                    int cars      = buf[i*3+2];
                    if(lightIdx>=0 && lightIdx<L && minuteIdx>=0){
                        int h = (int)((1LL*minuteIdx * stepMin) / 60);
                        if(h>=0 && h<H) local[(size_t)h * L + lightIdx] += cars;
                    }
                }
            }
            // signal READY for more work
            int one=1; MPI_Send(&one, 1, MPI_INT, 0, TAG_READY, MPI_COMM_WORLD);
        }
    }

    // Contribute to the reduction
    MPI_Reduce(local.data(), nullptr, H*L, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
}

int main(int argc, char** argv){
    MPI_Init(&argc, &argv);
    int rank=0, world=1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world);

    if(rank==0){
        if(argc < 5){
            cerr << "Usage: ./mpi_traffic <csv> <topN> <stepMin> <batchSize> [--async]\n";
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    // Broadcast args presence is trivial; only master needs to parse file.
    string csv; int topN=0, stepMin=5, batchSize=20000;
    bool asyncMode=false;

    if(rank==0){
        csv       = argv[1];
        topN      = stoi(argv[2]);
        stepMin   = stoi(argv[3]);
        batchSize = stoi(argv[4]);
        if(argc>=6 && string(argv[5])=="--async") asyncMode = true;
    }

    // Broadcast small params to all
    int asyncFlag = asyncMode ? 1 : 0;
    int csvLen = (rank==0 ? (int)csv.size() : 0);
    MPI_Bcast(&topN, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&stepMin, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&batchSize, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&asyncFlag, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&csvLen, 1, MPI_INT, 0, MPI_COMM_WORLD);
    vector<char> csvbuf(csvLen+1, 0);
    if(rank==0) memcpy(csvbuf.data(), csv.c_str(), csvLen);
    MPI_Bcast(csvbuf.data(), csvLen+1, MPI_CHAR, 0, MPI_COMM_WORLD);
    if(rank!=0) csv = string(csvbuf.data());

    // Discover dimensions on master, then broadcast H and L
    int H=0, L=0; long long skipped1=0, skipped2=0;
    vector<Rec> recs;
    if(rank==0){
        try{
            discover_dims(csv, stepMin, H, L, skipped1);
            recs = read_all_recs(csv, stepMin, skipped2);
        }catch(const exception& e){
            cerr << e.what() << "\n";
            MPI_Abort(MPI_COMM_WORLD, 2);
        }
    }
    MPI_Bcast(&H, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&L, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if(rank==0){
        if(asyncMode) master_async(recs, H, L, stepMin, topN, batchSize, world);
        else          master_blocking(recs, H, L, stepMin, topN, batchSize, world);
        if(skipped1+skipped2>0) cerr << "[mpi] skipped=" << (skipped1+skipped2) << " malformed lines\n";
    }else{
        worker_loop(rank, H, L, stepMin);
    }

    MPI_Finalize();
    return 0;
}