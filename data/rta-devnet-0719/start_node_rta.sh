#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

$DIR/graftnoded --testnet  \
   --config-file $DIR/node_data/testnet/graft.conf \
   --data-dir $DIR/node_data \
   --confirm-external-bind \
   --log-level 2,net.p2p:TRACE \
   --detach



