**Overview:**
This is a server-client program for file transfer between server and client. The server listens for file requests. When the client sends file requests along with the number of threads N, the server breaks the file into N chunks and sends them through N threads using N sockets. The file is reassembled at the client site. To verify that the file content is the same and has not been corrupted, the server and client use SHA256.

***Usage:***
1. First, run the server program using `./server`.
2. For file transfer, use the following format: `./client <fileName> <numThreads>`. The new file will be saved by the name `<newFile_at_client.fileExtension>`.

**Note:** If the file size is too large, sending it over fewer threads might not work.
