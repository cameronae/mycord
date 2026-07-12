This is an implementation of a terminal based chat service that any users with the same endpoint can connect and message eachother.

For functionality you could ssh onto a machine together and run server.py then each compile and build client.c

The best practice to sue this is to use ngrok.com to create a free endpoint.
## Using ngrok.com
--------------------
Create an account and etc... \
Once set up, run python3 server.py to start the sever and look for the port mycord is listening on \
ex:  mycord server listening on 0.0.0.0:12345 \
With that 12345, in another terminal window run ngrok tcp 12345 this creates the endpoint listening on that local port \
Then ngrok will create a public url like 0.tcp.ngrok.io:12345 \
finally run ./client --domain 0.tcp.ngrok.io --port 12345 \
DONE