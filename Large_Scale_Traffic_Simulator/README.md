# Traffic Control Simulator – SIT315 M2.T3D

## Overview
This project implements a Traffic Control Simulator using the **producer–consumer concurrency pattern**.  
It simulates traffic lights reporting the number of cars that pass every 5 minutes. Producer threads read traffic data records and insert them into a bounded buffer. Consumer threads process the records, aggregate totals, and compute the **Top-N busiest traffic lights per hour**.

The simulator demonstrates:
- Sequential vs concurrent implementations.
- Thread safety using `mutex` and `condition_variable`.
- Blocking vs non-blocking operations.
- Flexible configuration of producers, consumers, and buffer capacity.

---

## File Structure
traffic-sim/
├─ gen.cpp # Traffic data generator
├─ sequential.cpp # Sequential baseline (reference)
├─ concurrent.cpp # Concurrent producer-consumer solution
├─ data.csv # Example generated traffic data
└─ README.md # This file

---

## Build Instructions
Compile the programs using `g++`:

```bash
g++ -O2 -std=gnu++17 gen.cpp -o gen
g++ -O2 -std=gnu++17 sequential.cpp -o seq
g++ -O2 -std=gnu++17 concurrent.cpp -o conc
