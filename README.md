# rpmbd – Simulated RPMB Device (CUSE/FUSE)

`rpmbd` provides a **simulated RPMB device** exposed as:

- `/dev/mmcblk2rpmb`

It is implemented via **CUSE/FUSE** so RPMB tooling can be developed and tested without real RPMB hardware.

---

## Clone

```bash
git clone https://github.com/lgoio/rpmbd.git
cd rpmbd
```

---

## Requirements (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  pkg-config \
  mmc-utils \
  libfuse3-dev \
  libssl-dev
```

---

## Build

Use the provided `build.sh` script:

```bash
./build.sh
```

Debug build:

```bash
./build.sh --debug
```

The resulting binary will be located at:

- `build/rpmbd`

---

## Run

For development, start the simulator via `run.sh`.

### Default (fresh state)

Starts `rpmbd` and **removes** any existing state file before launching:

```bash
./run.sh
```

This creates/uses the state file in the **current working directory**:

- `./rpmb_state.bin`

### Keep state file

Starts `rpmbd` **without deleting** the state file:

```bash
./run.sh -k
```

---

## Test (mmc-utils)

A small helper script is provided to exercise basic RPMB operations via `mmc-utils`
against the simulated device:

```bash
./test.sh
```

The test script will create `key.bin` and `data.bin` in the working directory if they
do not exist yet, then run a simple write/read/compare sequence using:

- `mmc rpmb write-key`
- `mmc rpmb write-block`
- `mmc rpmb read-counter`
- `mmc rpmb read-block`

---

## Result

After starting, the simulated device is available at:

```bash
/dev/mmcblk2rpmb
```
