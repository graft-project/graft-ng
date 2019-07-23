# About RTA

RTA is a foundational feature of GRAFT Network, which allows real-time payments to be done at the point of sale, something that traditional blockchains aren’t capable of themselves. Being able to have predictable transaction time is fundamental to having digital currency work at the point of sale.

The other fundamental feature that RTA brings to the world of r/etail payments is predictable fees for the merchant and minimized fees to the buyer. Predictability (both in time and fees) and the overall smooth, point-of-sale friendly experience are key to having a functional payment network that can compete with traditional credit/debit payment networks like VISA, MC, Amex.

## Testing RTA

We are rolling out RTA testing gradually, focusing first on basic functionality and ramping up the testing scope from there to focus more on vulnerability and performance (there will be bounty programs), followed by beta testnet, security audit, and finally mainnet.

_**There’s also a [QuickStart guide](https://github.com/yidakee/docs/blob/master/Graft_Supernode_Testnet_Simple-step-by-step-setup-instructions-July2019.md) that the community has put together.**_


# Graft Supernode (graft-ng)

Supernodes represent the Proof-of-Stake Network layer for GRAFT Network.  
Their main function is instant authorizations.  Instant authorizations are accomplished via supernode sample-based consensus. Supernodes get rewarded with part of the sale transaction fee for their work.  Supernodes also provide DAPI into GRAFT Network, supporting various transaction types including and most notably a "Sale" transaction compatible with the Point-of-Sale environments. 

GRAFT Network itself is an attempt at building a payment network that functions similarly to other credit card networks with instant authorizations, merchant paid (greately reduced) transaction proportionate fees, multiple transaction types that are compatible with point-of-sale workflows, adaptable to regulatory environments and with unlimited TPS via decentralization.



## Compiling Graft Supernode from Source

### Dependencies

**Due to gcc 7.3.0 being a hard requirement, we strongly recommend to use Ubuntu 18.04 as a build platform***

| Dependency     | Min. Version  | Debian/Ubuntu Pkg  | Arch Pkg       | Optional | Purpose                |
| -------------- | ------------- | ------------------ | -------------- | -------- | ---------------------- |
| GCC            | 7.3.0         | `build-essential`  | `base-devel`   | NO       |                        |
| CMake          | 3.10.2        | `cmake`^           | `cmake`        | NO       |                        |
| pkg-config     | any           | `pkg-config`       | `base-devel`   | NO       |                        |
| Boost          | 1.65          | `libboost-all-dev` | `boost`        | NO       | C++ libraries          |
| OpenSSL        | basically any | `libssl-dev`       | `openssl`      | NO       |                        |
| autoconf       | any           | `autoconf`         |                | NO       | libr3 dependency       |
| automake       | any           | `automake`         |                | NO       | libr3 dependency       |
| check          | any           | `check`            |                | NO       | libr3 dependency       |
| PCRE3          | any           | `libpcre3-dev`     |                | NO       | libr3 dependency       |
| RapidJson      | 1.1.0         | `rapidjson-dev`    |                | NO       |                        |
| Readline       | 7.0           | `libreadline-dev`  |                | NO       | command line interface |

[^] Some Debian/Ubuntu versions (for example, Ubuntu 16.04) don't support CMake 3.10.2 from the package. To install it manually see **Install non-standard dependencies** bellow.

### Install non-standard dependencies

#### CMake 3.10.2
Go to the download page on the CMake official site https://cmake.org/download/ and download sources (.tar.gz) or installation script (.sh) for CMake 3.10.2 or later.

If you downloaded sources, unpack them and follow the installation instruction from CMake. If you downloaded installation script, run following command to install CMake:

```bash
sudo /bin/sh cmake-3.10.2-Linux-x86_64.sh --prefix=/opt/cmake --skip-license
```

If you don't want to download the installation script manually, you can simply run following command from the command line (it requires curl and you must accept license by yourself):

```bash
curl -s https://cmake.org/files/v3.12/cmake-3.10.2-Linux-x86_64.sh | bash -e
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
cmake  <project root> -DOPT_BUILD_TESTS=ON
make all
```

Then execute *graft_server_test* to run the tests.

```bash
<build directory>/graft_server_test
```

## Detailed Installation [Instructions](https://github.com/graft-project/graft-ng/wiki/Instructions)
See [Instructions](https://github.com/graft-project/graft-ng/wiki/Instructions) for more details.
