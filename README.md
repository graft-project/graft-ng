# Graft Supernode (graft-ng)

## Compiling Graft Supernode from Source

### Dependencies

| Dependency     | Min. Version  | Debian/Ubuntu Pkg  | Arch Pkg       | Optional | Purpose          |
| -------------- | ------------- | ------------------ | -------------- | -------- | ---------------- |
| GCC            | 4.7.3         | `build-essential`  | `base-devel`   | NO       |                  |
| CMake          | 3.11.0        | `cmake`^           | `cmake`        | NO       |                  |
| pkg-config     | any           | `pkg-config`       | `base-devel`   | NO       |                  |
| Boost          | 1.58          | `libboost-all-dev` | `boost`        | NO       | C++ libraries    |
| OpenSSL        | basically any | `libssl-dev`       | `openssl`      | NO       | sha256 sum       |
| autoconf       | any           | `autoconf`         |                | NO       | libr3 dependency |
| automake       | any           | `automake`         |                | NO       | libr3 dependency |
| check          | any           | `check`            |                | NO       | libr3 dependency |
| PCRE3          | any           | `libpcre3`         |                | NO       | libr3 dependency |
| PCRE3 devel    | any           | `libpcre3-dev`     |                | NO       | libr3 dependency |

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

### Build instructions

#### Linux (Ubuntu)
#### MacOS
#### Windows
