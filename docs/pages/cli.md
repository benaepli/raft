\page CLI "CLI Overview"

# CLI Overview

The CLI is an easy demo of a key-value store built over Raft.

## Usage

There are two primary subcommands:

1. `serve`
2. `client`

### Serve Subcommand

Each invocation of `serve` starts a server instance.
It accepts a TOML cluster configuration file and the node ID to control. For instance:

```bash
raft-cli serve --config raft.toml --node-id node-1 --threads 3
```

where `threads` is an optional parameter that defaults to `1`.

Our TOML configuration file is defined as follows:

```toml
[settings]
election_timeout_ms = 1000 # Election timeout in milliseconds
heartbeat_interval_ms = 50 # Heartbeat interval in milliseconds
data_directory = "/var/lib/raft-data" # Directory for storing persistent data

[[cluster.nodes]]
id = "node-1"
address = "10.0.1.1"
kv_port = 4000
raft_port = 5000

[[cluster.nodes]]
id = "node-2"
address = "10.0.1.2"
kv_port = 4000
raft_port = 5000

[[cluster.nodes]]
id = "node-3"
address = "10.0.1.3"
kv_port = 4000
raft_port = 5000
```

So in the previous example, calling serve on node-1 will start a Raft server on port `5000` and a key-value store on
port `4000`.

### Client Subcommand

Simply execute one of the following commands:

```bash 
# The client will try connecting to the first node in the list to find the leader.
raft-cli client --config raft.toml
```

Or:

```bash
raft-cli client --address 10.0.1.1:4000
```

Both commands will launch an interactive shell where you can execute commands against the key-value store.
In the second case, the CLI will automatically redirect its requests to the leader node.

The following instructions are available:

- `put <key> <value>`
- `get <key>`
- `delete <key>`
- `exit`