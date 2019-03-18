# Graft Supernode (graft-ng)

## Compiling Graft Supernode from Source

### Dependencies

**Due to gcc 7.3.0 being a hard requirement, we strongly recommend to use Ubuntu 18.04 as a build platform***

| Dependency     | Min. Version  | Debian/Ubuntu Pkg  | Arch Pkg       | Optional | Purpose                |
| -------------- | ------------- | ------------------ | -------------- | -------- | ---------------------- |
| GCC            | 7.3.0         | `build-essential`  | `base-devel`   | NO       |                        |
| CMake          | 3.11.0        | `cmake`^           | `cmake`        | NO       |                        |
| pkg-config     | any           | `pkg-config`       | `base-devel`   | NO       |                        |
| Boost          | 1.65          | `libboost-all-dev` | `boost`        | NO       | C++ libraries          |
| OpenSSL        | basically any | `libssl-dev`       | `openssl`      | NO       |                        |
| autoconf       | any           | `autoconf`         |                | NO       | libr3 dependency       |
| automake       | any           | `automake`         |                | NO       | libr3 dependency       |
| check          | any           | `check`            |                | NO       | libr3 dependency       |
| PCRE3          | any           | `libpcre3-dev`     |                | NO       | libr3 dependency       |
| RapidJson      | 1.1.0         | `rapidjson-dev`    |                | NO       |                        |
| Readline       | 7.0           | `libreadline-dev`  |                | NO       | command line interface |

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
git clone --recurse-submodules https://github.com/graft-project/graft-ng.git
```

### Build instructions

#### Linux (Ubuntu)

To build graft_server, please run the following commands:

```bash
mkdir -p <build directory>
cd <build directory>
cmake  <project root>
make
```

If you want to build unit test suit as well, run *cmake* with additional parameter:

```bash
mkdir -p <build directory>
cd <build directory>
cmake  <project root> -DOPT_BUILD_TEST
make --all
```

Then execute *graft_server_test* to run the tests.

```bash
<build directory>/graft_server_test
```

#### MacOS
#### Windows
