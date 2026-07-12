# mycord

This is an implementation of a terminal-based chat service that allows users with the same endpoint to connect and message each other.

## Compiling

To compile, run:

```bash
gcc client.c tui.c -o client
```

The recommended way to use this is to use ngrok.com to create a free endpoint.

## Using ngrok.com

1. Create an account and set up ngrok.
2. Start the server:

```bash
python3 server.py
```

Look for the port that mycord is listening on, for example:

```text
mycord server listening on 0.0.0.0:12345
```

3. In another terminal window, run:

```bash
ngrok tcp 12345
```

This creates the endpoint listening on that local port.

4. Ngrok will create a public URL like:

```text
0.tcp.ngrok.io:12345
```

5. Connect with the client:

```bash
./client --domain 0.tcp.ngrok.io --port 12345
```

## Local

The other way to use mycord is to start the server and connect to the local port.

```bash
python3 server.py
```

You should see something like:

```text
mycord server listening on 0.0.0.0:12345
```

Then connect with:

```bash
./client --port 12345
```

or:

```bash
./client --port 12345 --tui
```