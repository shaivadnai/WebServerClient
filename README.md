# WebServerClient

Written as part of the Operating Systems course (COM 3610)

Authors:
Jonathan Haller,
Meir Jacobs,
Charles Vadnai 

Design Overview:

Client:

    Main creates the N requester threads for either the FIFO or CONCUR scheduling function (as defined by arguments passed to client and runtime). 

    Both FIFO and CONCUR utilize pthread_barrier_wait functions to ensure all threads sync between requet rounds.

    FIFO - implements a semaphore to ensure only one thread makes a request at a time.



Server:
    Main forks to create a daemon then creates a producer thread. 
    This producer thread creates the threadpool containing N consumer threads.

    Producer and Consumer threads work out of a Buffer of type struct Connection_Descriptor. Connection Descriptors contain a number of values: most critically the socketfd of the connection, whether the buffer element is occupied, the extension of the file requested, the request itself and a variety of values for statistics.
	
    When a request comes in, the producer validates the request and extracts the extension of the file requested. It then acquires a mutex_lock on the buffer of Connection Descriptors and inserts the newly created connection descriptor at the first available spot in the buffer array. It then unlocks the mutex and wakes up all consumers.

    When a consumer awakens, it gets a mutex lock on the Connection_Descriptor Buffer and verifies that there is a Connection Descriptor in the buffer. It then proceeds to follow the specified scheduling function and utilitzes the Connection Descriptor's extension attribute to determine each Request File type or the arrival sequence of the Connection Descriptor in question - dependent on said scheduling function. The consumer then marks the corresponding element in the buffer array as unoccupied, unlocks the mutex and awakens the producer - then continues to respond to the request.

    ANY - FIFO
    FIFO - Iterate over all elements in buffer, find element with lowest count and return
    HPIC - Iterate over all elements in buffer, find Image element with lowest count and return - if no image files, defaults to FIFO
    HPHC - Iterate over all elements in buffer, find Text element with lowest count and return - if no text files, defaults to FIFO


Testing:
    To test scheduling options, we prevented the consumer threads from accessing the buffer until the buffer was filled at least once. This allowed us to check if the requests were returned as expected.
    
    ANY/FIFO - Used CONCUR client to make many requests from the server and checked which got returned.

    HPIC/HPHC - Used browser to make alternating requests of images and text files. We used the browser for this process because images can't be easily displayed in the terminal.

    Arguments:
        FIFO - ./server portnum 2 10 FIFO &
             - ./client localhost portnum 10 CONCUR <text>
             - Output: Statitics would indicate that complete count was    iterating sequentially
        HPIC - ./server portnum 2 10 HPIC &
             - In Browser: Request 3 html files, followed by 3 images
             - Output: Fulfill 1 or 2 text requests, followed by 3 images, then continue to fulfill the remaining text requests
        HPHC - ./server portnum 2 10 FIFO &
             - In Browser: Request 3 html files, followed by 3 images
             - Output: Fulfill 1 or 2 image requests, followed by 3 text, then continue to fulfill the remaining image requests
