# About RTA

RTA is a foundational feature of GRAFT Network, which allows real-time payments to be done at the point of sale, something that traditional blockchains aren’t capable of themselves. Being able to have predictable transaction time is fundamental to having digital currency work at the point of sale.

The other fundamental feature that RTA brings to the world of r/etail payments is predictable fees for the merchant and minimized fees to the buyer. Predictability (both in time and fees) and the overall smooth, point-of-sale friendly experience are key to having a functional payment network that can compete with traditional credit/debit payment networks like VISA, MC, Amex.

## Testing RTA

We are rolling out RTA testing gradually, focusing first on basic functionality and ramping up the testing scope from there to focus more on vulnerability and performance (there will be bounty programs), followed by beta testnet, security audit, and finally mainnet.

_**There’s also a [QuickStart guide](https://github.com/yidakee/docs/blob/master/Graft_Supernode_Testnet_Simple-step-by-step-setup-instructions-July2019.md) that the community has put together.**_


## About

RTA implements the RTA transaction validation flow as described in: https://github.com/graft-project/DesignDocuments/blob/develop/RFCs/%5BRFC-003-RTVF%5D-RTA-Transaction-Validation-Flow.md


## Known issues:
- Disqualification transactions implementation not merged yet
- Jump-list communications implementation not merged yet
- RTA validation on cryptonode is shortcut temporarily
- Transaction rejection by POS is not implemented yet

## Building and installation

>  NOTE: you can use prebuilt Ubuntu 18.04 x64 binaries: http://graftmainnet.s3.us-east-2.amazonaws.com/rta-devnet-0719/rta_beta20190722_1.tar.gz 

1. Follow the instructions on how to build supernode in https://github.com/graft-project/graft-ng/wiki/Supernode-Install-&-Usage-Instruction, using _**develop-rta0719**_ branch

2. Create directory e.g. `$HOME/graft/rta-beta0719`
```ruby
mkdir -p $HOME/graft/rta-beta0719
```
3. [_optional_] download rta blockchain: http://graftmainnet.s3.us-east-2.amazonaws.com/rta-devnet-0719/lmdb.tar.gz 

```ruby
wget http://graftmainnet.s3.us-east-2.amazonaws.com/rta-devnet-0719/lmdb.tar.gz
```

4. Unpack it to `$HOME/graft/rta-beta0719/node_data/testnet/lmdb`

5. Copy binaries  `$BUILDDIR/supernode`, `$BUILDDIR/config.ini`, `$BUILDDIR/rta-pos-cli`, `$BUILDDIR/rta-wallet-cli`, `$BUILDDIR/BUILD/bin/*` to `$HOME/graft/rta-beta0719` .
 Copy directory `$BUILDDIR/graftlets` to `$HOME/graft/rta-beta0719`

 
6. Use following config file to run graftnoded: https://github.com/graft-project/graft-ng/blob/develop-rta0719/data/rta-devnet-0719/graft.conf  - copy it to `$HOME/graft/rta-beta0719/node_data/testnet`.  
We also prepared the script to run cryptonode: https://github.com/graft-project/graft-ng/blob/develop-rta0719/data/rta-devnet-0719/start_node_rta.sh . Copy it to `$HOME/graft/rta-beta0719/` and run;  Make sure your graftnoded synced with this network: http://54.88.179.186:8081/

7. Create wallet and request funds by contacting admins in the main TG group. 

> Please note - cryptonode started with config above runs RPC on port 28681 so you'll have to specify it explicitly while working with wallet (`--daemon-address localhost:28681`)

8. Create wallets for supernode,  PoS and Wallet

9. Setup supernode using manual: https://github.com/graft-project/graft-ng/wiki/Supernode-Install-&-Usage-Instruction#graft-supernode-configuration

10.[_optional_] Send stake amount to supernode's wallet

11. Send some amount to the Wallet's wallet

## Running and testing

1. Make sure cryptonode and supernode running, check your local supernode with request:
```ruby
curl http://localhost:28690/debug/supernode_list/1
```
Make sure you see your supernode in the output

2. Open another terminal (or new window in tmux/screen), go to directory where you copied binaries (`$BINDIR`), copy wallet files you created previously here and run **"rta-wallet-cli"** like this:
```ruby
./rta-wallet-cli --wallet-path <your-wallet-name>
```
By default it expects supernode available at `localhost:28690`, cryptonode's RPC at `localhost:28681`. It will open wallet, refresh it,  print the balance and wait for a user pressed `<Return>` key to continue processing

3. Open one more terminal (or new window in tmux/screen), pick wallet address you created for a PoS, go to directory with binaries (`$BINDIR`), and run:
```ruby
./rta-pos-cli --wallet-address <wallet-address-PoS> --amount <sale-amount>
```
This will initiate the sale. Right after this switch to the terminal with "waiting" **rta-wallet-cli** app and press `<Return>`. This will process the payment.

4. Next switch to the terminal with **rta-pos-cli** and see how Sale is processed.
