# Lab_4
# Project README

## Overview
Adding functionality to our system

## Usage

### Compilation

```bash
make
```

### Running the Simulation

1. **Run the `oss` process:**

   ```bash
   ./oss -n 3 -s 5 -t 7 -i 100 -f log.txt
   ```

2. **Open a terminal for each worker:**

   ```bash
   ./worker 5 500000
   ```

### Cleanup

```bash
make clean
```

## Example

- Simulate an environment with 3 workers:

   ```bash
   ./oss -s 5 
   ```

