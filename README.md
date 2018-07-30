# Graft Supernode (graft-ng)

## Compiling Graft Supernode from Source

### Dependencies

| Dependency     | Min. Version  | Debian/Ubuntu Pkg  | Arch Pkg       | Optional | Purpose          |
| -------------- | ------------- | ------------------ | -------------- | -------- | ---------------- |
| GCC            | 4.7.3         | `build-essential`  | `base-devel`   | NO       |                  |
| CMake          | 3.11.0        | `cmake`^           | `cmake`        | NO       |                  |
| pkg-config     | any           | `pkg-config`       | `base-devel`   | NO       |                  |
| Boost          | 1.65          | `libboost-all-dev` | `boost`        | NO       | C++ libraries    |
| OpenSSL        | basically any | `libssl-dev`       | `openssl`      | NO       |                  |
| autoconf       | any           | `autoconf`         |                | NO       | libr3 dependency |
| automake       | any           | `automake`         |                | NO       | libr3 dependency |
| check          | any           | `check`            |                | NO       | libr3 dependency |
| PCRE3          | any           | `libpcre3-dev`     |                | NO       | libr3 dependency |
| RapidJson      | 1.1.0         | `rapidjson-dev`    |                | NO       |                  |

[^] Some Debian/Ubuntu versions (for example, Ubuntu 16.04) don't support CMake 3.11.0 from the package. To install it manually see **Install non-standard dependencies** bellow.

### Install non-standard dependencies

#### CMake 3.11.0
Go to the download page on the CMake official site https://cmake.org/download/ and download sources (.tar.gz) or installation script (.sh) for CMake 3.11.0 or later.

If you downloaded sources, unpack them and follow the installation instruction from CMake. If you downloaded installation script, run following command to install CMake:

```bash
sudo /bin/sh cmake-3.11.1-Linux-x86_64.sh --prefix=/opt/cmake --skip-license
```

If you don't want to download the installation script manually, you can simply run following command from the command line (it requires curl and you must accept license by yourself):

```bash
curl -s https://cmake.org/files/v3.11/cmake-3.11.1-Linux-x86_64.sh | bash -e
```
### Prepare sources

Clone repository:

```bash
git clone https://github.com/graft-project/graft-ng.git
```
Initialize and update submodules:

```bash
git submodule init
git submodule update --recursive
```

### Build instructions

To build graft_server run *cmake* and then *make* like following

```bash
cmake ..
make
```

If you want to build graft_server_test also, run *cmake* with additional parameter

```bash
cmake .. -DOPT_BUILD_TEST
make --all
```

#### Linux (Ubuntu)
#### MacOS
#### Windows
