/*******************************************************************************
 * Copyright (c) 2009, 2022 IBM Corp., Ian Craggs and others
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    https://www.eclipse.org/legal/epl-2.0/
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial implementation and documentation
 *    Ian Craggs - async client updates
 *    Ian Craggs - fix for bug 484496
 *    Juergen Kosel, Ian Craggs - fix for issue #135
 *    Ian Craggs - issue #217
 *    Ian Craggs - fix for issue #186
 *    Ian Craggs - remove StackTrace print debugging calls
 *******************************************************************************/

/**
 * @file
 * \brief Socket related functions
 *
 * Some other related functions are in the SocketBuffer module
 */


#include "Socket.h"
#include "Log.h"
#include "SocketBuffer.h"
#include "Messages.h"
#include "StackTrace.h"
#if defined(OPENSSL)
#include "SSLSocket.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>

#include "Heap.h"

#if defined(USE_SELECT)
int isReady(int socket, fd_set* read_set, fd_set* write_set);
int Socket_continueWrites(fd_set* pwset, SOCKET* socket, mutex_type mutex);
#else
int isReady(int index);
int Socket_continueWrites(SOCKET* socket, mutex_type mutex);
#endif
int Socket_setnonblocking(SOCKET sock);
int Socket_error(char* aString, SOCKET sock);
int Socket_addSocket(SOCKET newSd);
int Socket_writev(SOCKET socket, iobuf* iovecs, int count, unsigned long* bytes);
int Socket_close_only(SOCKET socket);
int Socket_continueWrite(SOCKET socket);
char* Socket_getaddrname(struct sockaddr* sa, SOCKET sock);
int Socket_abortWrite(SOCKET socket);

#if defined(_WIN32) || defined(_WIN64)
#define iov_len len
#define iov_base buf
#define snprintf _snprintf
#endif

/**
 * Structure to hold all socket data for this module
 */
Sockets mod_s;
#if defined(USE_SELECT)
static fd_set wset;
#endif

extern mutex_type socket_mutex;

/**
 * Set a socket non-blocking, OS independently
 * @param sock the socket to set non-blocking
 * @return TCP call error code
 */
int Socket_setnonblocking(SOCKET sock)
{
	int rc;
#if defined(_WIN32) || defined(_WIN64)
	u_long flag = 1L;

	FUNC_ENTRY;
	rc = ioctl(sock, FIONBIO, &flag);
#else
	int flags;

	FUNC_ENTRY;
	if ((flags = fcntl(sock, F_GETFL, 0)))
		flags = 0;
	rc = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Gets the specific error corresponding to SOCKET_ERROR
 * @param aString the function that was being used when the error occurred
 * @param sock the socket on which the error occurred
 * @return the specific TCP error code
 */
int Socket_error(char* aString, SOCKET sock)
{
	int err;

#if defined(_WIN32) || defined(_WIN64)
	err = WSAGetLastError();
#else
	err = errno;
#endif
	if (err != EINTR && err != EAGAIN && err != EINPROGRESS && err != EWOULDBLOCK)
	{
		if (strcmp(aString, "shutdown") != 0 || (err != ENOTCONN && err != ECONNRESET))
			Log(TRACE_MINIMUM, -1, "Socket error %s(%d) in %s for socket %d", strerror(err), err, aString, sock);
	}
	return err;
}


/**
 * Initialize the socket module
 */
void Socket_outInitialize(void)
{
#if defined(_WIN32) || defined(_WIN64)
	WORD    winsockVer = 0x0202;
	WSADATA wsd;

	FUNC_ENTRY;
	WSAStartup(winsockVer, &wsd);
#else
	FUNC_ENTRY;
	signal(SIGPIPE, SIG_IGN);
#endif

	SocketBuffer_initialize();
	mod_s.connect_pending = ListInitialize();
	mod_s.write_pending = ListInitialize();
	
#if defined(USE_SELECT)
	mod_s.clientsds = ListInitialize();
	mod_s.cur_clientsds = NULL;
	FD_ZERO(&(mod_s.rset));														/* Initialize the descriptor set */
	FD_ZERO(&(mod_s.pending_wset));
	mod_s.maxfdp1 = 0;
	memcpy((void*)&(mod_s.rset_saved), (void*)&(mod_s.rset), sizeof(mod_s.rset_saved));
#else
	mod_s.nfds = 0;
	mod_s.fds_read = NULL;
	mod_s.fds_write = NULL;

	mod_s.saved.cur_fd = -1;
	mod_s.saved.fds_write = NULL;
	mod_s.saved.fds_read = NULL;
	mod_s.saved.nfds = 0;
#endif
	FUNC_EXIT;
}


/**
 * Terminate the socket module
 */
void Socket_outTerminate(void)
{
	FUNC_ENTRY;
	ListFree(mod_s.connect_pending);
	ListFree(mod_s.write_pending);
#if defined(USE_SELECT)
	ListFree(mod_s.clientsds);
#else
	if (mod_s.fds_read)
		free(mod_s.fds_read);
	if (mod_s.fds_write)
		free(mod_s.fds_write);
	if (mod_s.saved.fds_write)
		free(mod_s.saved.fds_write);
	if (mod_s.saved.fds_read)
		free(mod_s.saved.fds_read);
#endif
	SocketBuffer_terminate();
#if defined(_WIN32) || defined(_WIN64)
	WSACleanup();
#endif
	FUNC_EXIT;
}


#if defined(USE_SELECT)
/**
 * Add a socket to the list of socket to check with select
 * @param newSd the new socket to add
 */
int Socket_addSocket(SOCKET newSd)
{
	int rc = 0;

	FUNC_ENTRY;
	if (ListFindItem(mod_s.clientsds, &newSd, intcompare) == NULL) /* make sure we don't add the same socket twice */
	{
		if (mod_s.clientsds->count >= FD_SETSIZE)
		{
			Log(LOG_ERROR, -1, "addSocket: exceeded FD_SETSIZE %d", FD_SETSIZE);
			rc = SOCKET_ERROR;
		}
		else
		{
			SOCKET* pnewSd = (SOCKET*)malloc(sizeof(newSd));

			if (!pnewSd)
			{
				rc = PAHO_MEMORY_ERROR;
				goto exit;
			}
			*pnewSd = newSd;
			if (!ListAppend(mod_s.clientsds, pnewSd, sizeof(newSd)))
			{
				free(pnewSd);
				rc = PAHO_MEMORY_ERROR;
				goto exit;
			}
			FD_SET(newSd, &(mod_s.rset_saved));
			mod_s.maxfdp1 = max(mod_s.maxfdp1, (int)newSd + 1);
			rc = Socket_setnonblocking(newSd);
			if (rc == SOCKET_ERROR)
				Log(LOG_ERROR, -1, "addSocket: setnonblocking");
		}
	}
	else
		Log(LOG_ERROR, -1, "addSocket: socket %d already in the list", newSd);

exit:
	FUNC_EXIT_RC(rc);
	return rc;
}
#else
static int cmpfds(const void *p1, const void *p2)
{
   SOCKET key1 = ((struct pollfd*)p1)->fd;
   SOCKET key2 = ((struct pollfd*)p2)->fd;

   return (key1 == key2) ? 0 : ((key1 < key2) ? -1 : 1);
}


static int cmpsockfds(const void *p1, const void *p2)
{
   int key1 = *(int*)p1;
   SOCKET key2 = ((struct pollfd*)p2)->fd;

   return (key1 == key2) ? 0 : ((key1 < key2) ? -1 : 1);
}


/**
 * Add a socket to the list of socket to check with select
 * @param newSd the new socket to add
 */
int Socket_addSocket(SOCKET newSd)
{
	int rc = 0;

	FUNC_ENTRY;
	Thread_lock_mutex(socket_mutex);
	mod_s.nfds++;
	if (mod_s.fds_read)
		mod_s.fds_read = realloc(mod_s.fds_read, mod_s.nfds * sizeof(mod_s.fds_read[0]));
	else
		mod_s.fds_read = malloc(mod_s.nfds * sizeof(mod_s.fds_read[0]));
	if (!mod_s.fds_read)
	{
		rc = PAHO_MEMORY_ERROR;
		goto exit;
	}
	if (mod_s.fds_write)
		mod_s.fds_write = realloc(mod_s.fds_write, mod_s.nfds * sizeof(mod_s.fds_write[0]));
	else
		mod_s.fds_write = malloc(mod_s.nfds * sizeof(mod_s.fds_write[0]));
	if (!mod_s.fds_read)
	{
		rc = PAHO_MEMORY_ERROR;
		goto exit;
	}

	mod_s.fds_read[mod_s.nfds - 1].fd = newSd;
	mod_s.fds_write[mod_s.nfds - 1].fd = newSd;
#if defined(_WIN32) || defined(_WIN64)
	mod_s.fds_read[mod_s.nfds - 1].events = POLLIN;
	mod_s.fds_write[mod_s.nfds - 1].events = POLLOUT;
#else
	mod_s.fds_read[mod_s.nfds - 1].events = POLLIN | POLLNVAL;
	mod_s.fds_write[mod_s.nfds - 1].events = POLLOUT;
#endif

	/* sort the poll fds array by socket number */
	qsort(mod_s.fds_read, (size_t)mod_s.nfds, sizeof(mod_s.fds_read[0]), cmpfds);
	qsort(mod_s.fds_write, (size_t)mod_s.nfds, sizeof(mod_s.fds_write[0]), cmpfds);

	rc = Socket_setnonblocking(newSd);
	if (rc == SOCKET_ERROR)
		Log(LOG_ERROR, -1, "addSocket: setnonblocking");

exit:
	Thread_unlock_mutex(socket_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}
#endif


#if defined(USE_SELECT)
/**
 * Don't accept work from a client unless it is accepting work back, i.e. its socket is writeable
 * this seems like a reasonable form of flow control, and practically, seems to work.
 * @param socket the socket to check
 * @param read_set the socket read set (see select doc)
 * @param write_set the socket write set (see select doc)
 * @return boolean - is the socket ready to go?
 */
int isReady(int socket, fd_set* read_set, fd_set* write_set)
{
	int rc = 1;

	FUNC_ENTRY;
	if  (ListFindItem(mod_s.connect_pending, &socket, intcompare) && FD_ISSET(socket, write_set))
		ListRemoveItem(mod_s.connect_pending, &socket, intcompare);
	else
		rc = FD_ISSET(socket, read_set) && FD_ISSET(socket, write_set) && Socket_noPendingWrites(socket);
	FUNC_EXIT_RC(rc);
	return rc;
}
#else
/**
 * Don't accept work from a client unless it is accepting work back, i.e. its socket is writeable
 * this seems like a reasonable form of flow control, and practically, seems to work.
 * @param index the socket index to check
 * @return boolean - is the socket ready to go?
 */
int isReady(int index)
{
	int rc = 1;
	SOCKET* socket = &mod_s.saved.fds_write[index].fd;

	FUNC_ENTRY;

	if ((mod_s.saved.fds_read[index].revents & POLLHUP) || (mod_s.saved.fds_read[index].revents & POLLNVAL))
		; /* signal work to be done if there is an error on the socket */
	else if  (ListFindItem(mod_s.connect_pending, socket, intcompare) &&
			(mod_s.saved.fds_write[index].revents & POLLOUT))
		ListRemoveItem(mod_s.connect_pending, socket, intcompare);
	else
		rc = (mod_s.saved.fds_read[index].revents & POLLIN) &&
			 (mod_s.saved.fds_write[index].revents & POLLOUT) &&
			 Socket_noPendingWrites(*socket);

	FUNC_EXIT_RC(rc);
	return rc;
}
#endif


#if defined(USE_SELECT)
/**
 *  Returns the next socket ready for communications as indicated by select
 *  @param more_work flag to indicate more work is waiting, and thus a timeout value of 0 should
 *  be used for the select
 *  @param timeout the timeout to be used for the select, unless overridden
 *  @param rc a value other than 0 indicates an error of the returned socket
 *  @return the socket next ready, or 0 if none is ready
 */
SOCKET Socket_getReadySocket(int more_work, int timeout, mutex_type mutex, int* rc)
{
	SOCKET sock = 0;
	*rc = 0;
	int timeout_ms = 1000;

	FUNC_ENTRY;
	Thread_lock_mutex(mutex);
	if (mod_s.clientsds->count == 0)
		goto exit;
		
	if (more_work)
		timeout_ms = 0;
	else if (timeout >= 0)
		timeout_ms = timeout;

	while (mod_s.cur_clientsds != NULL)
	{
		if (isReady(*((int*)(mod_s.cur_clientsds->content)), &(mod_s.rset), &wset))
			break;
		ListNextElement(mod_s.clientsds, &mod_s.cur_clientsds);
	}

	if (mod_s.cur_clientsds == NULL)
	{
		static struct timeval zero = {0L, 0L}; /* 0 seconds */
		int rc1, maxfdp1_saved;
		fd_set pwset;
		struct timeval timeout_tv = {0L, 0L};

		if (timeout_ms > 0L)
		{
			timeout_tv.tv_sec = timeout_ms / 1000;
			timeout_tv.tv_usec = (timeout_ms % 1000) * 1000; /* this field is microseconds! */
		}

		memcpy((void*)&(mod_s.rset), (void*)&(mod_s.rset_saved), sizeof(mod_s.rset));
		memcpy((void*)&(pwset), (void*)&(mod_s.pending_wset), sizeof(pwset));
		maxfdp1_saved = mod_s.maxfdp1;
		
		if (maxfdp1_saved == 0)
		{
			sock = 0;
			goto exit; /* no work to do */
		}
		/* Prevent performance issue by unlocking the socket_mutex while waiting for a ready socket. */
		Thread_unlock_mutex(mutex);
		*rc = select(maxfdp1_saved, &(mod_s.rset), &pwset, NULL, &timeout_tv);
		Thread_lock_mutex(mutex);
		if (*rc == SOCKET_ERROR)
		{
			Socket_error("read select", 0);
			goto exit;
		}
		Log(TRACE_MAX, -1, "Return code %d from read select", *rc);

		if (Socket_continueWrites(&pwset, &sock, mutex) == SOCKET_ERROR)
		{
			*rc = SOCKET_ERROR;
			goto exit;
		}

		memcpy((void*)&wset, (void*)&(mod_s.rset_saved), sizeof(wset));
		if ((rc1 = select(mod_s.maxfdp1, NULL, &(wset), NULL, &zero)) == SOCKET_ERROR)
		{
			Socket_error("write select", 0);
			*rc = rc1;
			goto exit;
		}
		Log(TRACE_MAX, -1, "Return code %d from write select", rc1);

		if (*rc == 0 && rc1 == 0)
		{
			sock = 0;
			goto exit; /* no work to do */
		}

		mod_s.cur_clientsds = mod_s.clientsds->first;
		while (mod_s.cur_clientsds != NULL)
		{
			int cursock = *((int*)(mod_s.cur_clientsds->content));
			if (isReady(cursock, &(mod_s.rset), &wset))
				break;
			ListNextElement(mod_s.clientsds, &mod_s.cur_clientsds);
		}
	}

	*rc = 0;
	if (mod_s.cur_clientsds == NULL)
		sock = 0;
	else
	{
		sock = *((int*)(mod_s.cur_clientsds->content));
		ListNextElement(mod_s.clientsds, &mod_s.cur_clientsds);
	}
exit:
	Thread_unlock_mutex(mutex);
	FUNC_EXIT_RC(sock);
	return sock;
} /* end getReadySocket */
#else
/**
 *  Returns the next socket ready for communications as indicated by select
 *  @param more_work flag to indicate more work is waiting, and thus a timeout value of 0 should
 *  be used for the select
 *  @param timeout the timeout to be used in ms
 *  @param rc a value other than 0 indicates an error of the returned socket
 *  @return the socket next ready, or 0 if none is ready
 */
SOCKET Socket_getReadySocket(int more_work, int timeout, mutex_type mutex, int* rc)
{
	SOCKET sock = 0;
	*rc = 0;
	int timeout_ms = 1000;

	FUNC_ENTRY;
	Thread_lock_mutex(mutex);
	if (mod_s.nfds == 0 && mod_s.saved.nfds == 0)
		goto exit;

	if (more_work)
		timeout_ms = 0;
	else if (timeout >= 0)
		timeout_ms = timeout;

	while (mod_s.saved.cur_fd != -1)
	{
		if (isReady(mod_s.saved.cur_fd))
			break;
		mod_s.saved.cur_fd = (mod_s.saved.cur_fd == mod_s.saved.nfds - 1) ? -1 : mod_s.saved.cur_fd + 1;
	}

	if (mod_s.saved.cur_fd == -1)
	{
		int rc1 = 0;

		if (mod_s.nfds != mod_s.saved.nfds)
		{
			mod_s.saved.nfds = mod_s.nfds;
			if (mod_s.saved.fds_read)
				mod_s.saved.fds_read = realloc(mod_s.saved.fds_read, mod_s.nfds * sizeof(struct pollfd));
			else
				mod_s.saved.fds_read = malloc(mod_s.nfds * sizeof(struct pollfd));
			if (mod_s.saved.fds_write)
				mod_s.saved.fds_write = realloc(mod_s.saved.fds_write, mod_s.nfds * sizeof(struct pollfd));
			else
				mod_s.saved.fds_write = malloc(mod_s.nfds * sizeof(struct pollfd));
		}
		memcpy(mod_s.saved.fds_read, mod_s.fds_read, mod_s.nfds * sizeof(struct pollfd));
		memcpy(mod_s.saved.fds_write, mod_s.fds_write, mod_s.nfds * sizeof(struct pollfd));

		if (mod_s.saved.nfds == 0)
		{
			sock = 0;
			goto exit; /* no work to do */
		}

		/* Check pending write set for writeable sockets */
		rc1 = poll(mod_s.saved.fds_write, mod_s.saved.nfds, 0);
		if (rc1 > 0 && Socket_continueWrites(&sock, mutex) == SOCKET_ERROR)
		{
			*rc = SOCKET_ERROR;
			goto exit;
		}

		/* Prevent performance issue by unlocking the socket_mutex while waiting for a ready socket. */
		Thread_unlock_mutex(mutex);
		*rc = poll(mod_s.saved.fds_read, mod_s.saved.nfds, timeout_ms);
		Thread_lock_mutex(mutex);
		if (*rc == SOCKET_ERROR)
		{
			Socket_error("poll", 0);
			goto exit;
		}
		Log(TRACE_MAX, -1, "Return code %d from poll", *rc);

		if (rc1 == 0 && *rc == 0)
		{
			sock = 0;
			goto exit; /* no work to do */
		}

		mod_s.saved.cur_fd = 0;
		while (mod_s.saved.cur_fd != -1)
		{
			if (isReady(mod_s.saved.cur_fd))
				break;
			mod_s.saved.cur_fd = (mod_s.saved.cur_fd == mod_s.saved.nfds - 1) ? -1 : mod_s.saved.cur_fd + 1;
		}
	}

	*rc = 0;
	if (mod_s.saved.cur_fd == -1)
		sock = 0;
	else
	{
		sock = mod_s.saved.fds_read[mod_s.saved.cur_fd].fd;
		mod_s.saved.cur_fd = (mod_s.saved.cur_fd == mod_s.saved.nfds - 1) ? -1 : mod_s.saved.cur_fd + 1;
	}
exit:
	Thread_unlock_mutex(mutex);
	FUNC_EXIT_RC(sock);
	return sock;
} /* end getReadySocket */
#endif


/**
 *  Reads one byte from a socket
 *  @param socket the socket to read from
 *  @param c the character read, returned
 *  @return completion code
 */
int Socket_getch(SOCKET socket, char* c)
{
	int rc = SOCKET_ERROR;

	FUNC_ENTRY;
	if ((rc = SocketBuffer_getQueuedChar(socket, c)) != SOCKETBUFFER_INTERRUPTED)
		goto exit;

	if ((rc = recv(socket, c, (size_t)1, 0)) == SOCKET_ERROR)
	{
		int err = Socket_error("recv - getch", socket);
		if (err == EWOULDBLOCK || err == EAGAIN)
		{
			rc = TCPSOCKET_INTERRUPTED;
			SocketBuffer_interrupted(socket, 0);
		}
	}
	else if (rc == 0)
		rc = SOCKET_ERROR; 	/* The return value from recv is 0 when the peer has performed an orderly shutdown. */
	else if (rc == 1)
	{
		SocketBuffer_queueChar(socket, *c);
		rc = TCPSOCKET_COMPLETE;
	}
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Attempts to read a number of bytes from a socket, non-blocking. If a previous read did not
 *  finish, then retrieve that data.
 *  @param socket the socket to read from
 *  @param bytes the number of bytes to read
 *  @param actual_len the actual number of bytes read
 *  @return completion code
 */
char *Socket_getdata(SOCKET socket, size_t bytes, size_t* actual_len, int *rc)
{
	char* buf;

	FUNC_ENTRY;
	if (bytes == 0)
	{
		buf = SocketBuffer_complete(socket);
		goto exit;
	}

	buf = SocketBuffer_getQueuedData(socket, bytes, actual_len);

	if ((*rc = recv(socket, buf + (*actual_len), (int)(bytes - (*actual_len)), 0)) == SOCKET_ERROR)
	{
		*rc = Socket_error("recv - getdata", socket);
		if (*rc != EAGAIN && *rc != EWOULDBLOCK)
		{
			buf = NULL;
			goto exit;
		}
	}
	else if (*rc == 0) /* rc 0 means the other end closed the socket, albeit "gracefully" */
	{
		buf = NULL;
		goto exit;
	}
	else
		*actual_len += *rc;

	if (*actual_len == bytes)
		SocketBuffer_complete(socket);
	else /* we didn't read the whole packet */
	{
		SocketBuffer_interrupted(socket, *actual_len);
		Log(TRACE_MAX, -1, "%d bytes expected but %d bytes now received", (int)bytes, (int)*actual_len);
	}
exit:
	FUNC_EXIT;
	return buf;
}


/**
 *  Indicate whether any data is pending outbound for a socket.
 *  @return boolean - true == no pending data.
 */
int Socket_noPendingWrites(SOCKET socket)
{
	SOCKET cursock = socket;
	return ListFindItem(mod_s.write_pending, &cursock, intcompare) == NULL;
}


/**
 *  Attempts to write a series of iovec buffers to a socket in *one* system call so that
 *  they are sent as one packet.
 *  @param socket the socket to write to
 *  @param iovecs an array of buffers to write
 *  @param count number of buffers in iovecs
 *  @param bytes number of bytes actually written returned
 *  @return completion code, especially TCPSOCKET_INTERRUPTED
 */
int Socket_writev(SOCKET socket, iobuf* iovecs, int count, unsigned long* bytes)
{
	int rc;

	FUNC_ENTRY;
	*bytes = 0L;
#if defined(_WIN32) || defined(_WIN64)
	rc = WSASend(socket, iovecs, count, (LPDWORD)bytes, 0, NULL, NULL);
	if (rc == SOCKET_ERROR)
	{
		int err = Socket_error("WSASend - putdatas", socket);
		if (err == EWOULDBLOCK || err == EAGAIN)
			rc = TCPSOCKET_INTERRUPTED;
	}
#else
/*#define TCPSOCKET_INTERRUPTED_TESTING
This section forces the occasional return of TCPSOCKET_INTERRUPTED,
for testing purposes only!
*/
#if defined(TCPSOCKET_INTERRUPTED_TESTING)
  static int i = 0;
	if (++i >= 10 && i < 21)
	{
		if (1)
		{
		  printf("Deliberately simulating TCPSOCKET_INTERRUPTED\n");
		  rc = TCPSOCKET_INTERRUPTED; /* simulate a network wait */
	  }
		else
		{
			printf("Deliberately simulating SOCKET_ERROR\n");
		  rc = SOCKET_ERROR;
		}
		/* should *bytes always be 0? */
		if (i == 20)
		{
		  printf("Shutdown socket\n");
		  shutdown(socket, SHUT_WR);
	  }
	}
	else
	{
#endif
	rc = writev(socket, iovecs, count);
	if (rc == SOCKET_ERROR)
	{
		int err = Socket_error("writev - putdatas", socket);
		if (err == EWOULDBLOCK || err == EAGAIN)
			rc = TCPSOCKET_INTERRUPTED;
	}
	else
		*bytes = rc;
#if defined(TCPSOCKET_INTERRUPTED_TESTING)
	}
#endif
#endif
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Attempts to write a series of buffers to a socket in *one* system call so that they are
 *  sent as one packet.
 *  @param socket the socket to write to
 *  @param buf0 the first buffer
 *  @param buf0len the length of data in the first buffer
 *  @param count number of buffers
 *  @param buffers an array of buffers to write
 *  @param buflens an array of corresponding buffer lengths
 *  @return completion code, especially TCPSOCKET_INTERRUPTED
 */
int Socket_putdatas(SOCKET socket, char* buf0, size_t buf0len, PacketBuffers bufs)
{
	unsigned long bytes = 0L;
	iobuf iovecs[5];
	int frees1[5];
	int rc = TCPSOCKET_INTERRUPTED, i;
	size_t total = buf0len;

	FUNC_ENTRY;
	if (!Socket_noPendingWrites(socket))
	{
		Log(LOG_SEVERE, -1, "Trying to write to socket %d for which there is already pending output", socket);
		rc = SOCKET_ERROR;
		goto exit;
	}

	for (i = 0; i < bufs.count; i++)
		total += bufs.buflens[i];

	iovecs[0].iov_base = buf0;
	iovecs[0].iov_len = (ULONG)buf0len;
	frees1[0] = 1; /* this buffer should be freed by SocketBuffer if the write is interrupted */
	for (i = 0; i < bufs.count; i++)
	{
		iovecs[i+1].iov_base = bufs.buffers[i];
		iovecs[i+1].iov_len = (ULONG)bufs.buflens[i];
		frees1[i+1] = bufs.frees[i];
	}

	if ((rc = Socket_writev(socket, iovecs, bufs.count+1, &bytes)) != SOCKET_ERROR)
	{
		if (bytes == total)
			rc = TCPSOCKET_COMPLETE;
		else
		{
			SOCKET* sockmem = (SOCKET*)malloc(sizeof(SOCKET));

			if (!sockmem)
			{
				rc = PAHO_MEMORY_ERROR;
				goto exit;
			}
			Log(TRACE_MIN, -1, "Partial write: %lu bytes of %lu actually written on socket %d",
					bytes, total, socket);
#if defined(OPENSSL)
			SocketBuffer_pendingWrite(socket, NULL, bufs.count+1, iovecs, frees1, total, bytes);
#else
			SocketBuffer_pendingWrite(socket, bufs.count+1, iovecs, frees1, total, bytes);
#endif
			*sockmem = socket;
			if (!ListAppend(mod_s.write_pending, sockmem, sizeof(int)))
			{
				free(sockmem);
				rc = PAHO_MEMORY_ERROR;
				goto exit;
			}
#if defined(USE_SELECT)
			FD_SET(socket, &(mod_s.pending_wset));
#endif
			rc = TCPSOCKET_INTERRUPTED;
		}
	}
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Add a socket to the pending write list, so that it is checked for writing in select.  This is used
 *  in connect processing when the TCP connect is incomplete, as we need to check the socket for both
 *  ready to read and write states.
 *  @param socket the socket to add
 */
void Socket_addPendingWrite(SOCKET socket)
{
#if defined(USE_SELECT)
	FD_SET(socket, &(mod_s.pending_wset));
#endif
}


/**
 *  Clear a socket from the pending write list - if one was added with Socket_addPendingWrite
 *  @param socket the socket to remove
 */
void Socket_clearPendingWrite(SOCKET socket)
{
#if defined(USE_SELECT)
	if (FD_ISSET(socket, &(mod_s.pending_wset)))
		FD_CLR(socket, &(mod_s.pending_wset));
#endif
}


/**
 *  Close a socket without removing it from the select list.
 *  @param socket the socket to close
 *  @return completion code
 */
int Socket_close_only(SOCKET socket)
{
	int rc;

	FUNC_ENTRY;
#if defined(_WIN32) || defined(_WIN64)
	if (shutdown(socket, SD_BOTH) == SOCKET_ERROR)
		Socket_error("shutdown", socket);
	if ((rc = closesocket(socket)) == SOCKET_ERROR)
		Socket_error("close", socket);
#else
	if (shutdown(socket, SHUT_WR) == SOCKET_ERROR)
		Socket_error("shutdown", socket);
	if ((rc = recv(socket, NULL, (size_t)0, 0)) == SOCKET_ERROR)
		Socket_error("shutdown", socket);
	if ((rc = close(socket)) == SOCKET_ERROR)
		Socket_error("close", socket);
#endif
	FUNC_EXIT_RC(rc);
	return rc;
}

#if defined(USE_SELECT)
/**
 *  Close a socket and remove it from the select list.
 *  @param socket the socket to close
 *  @return completion code
 */
int Socket_close(SOCKET socket)
{
	int rc = 0;

	FUNC_ENTRY;
	Socket_close_only(socket);
	FD_CLR(socket, &(mod_s.rset_saved));
	if (FD_ISSET(socket, &(mod_s.pending_wset)))
		FD_CLR(socket, &(mod_s.pending_wset));
	if (mod_s.cur_clientsds != NULL && *(int*)(mod_s.cur_clientsds->content) == socket)
		mod_s.cur_clientsds = mod_s.cur_clientsds->next;
	Socket_abortWrite(socket);
	SocketBuffer_cleanup(socket);
	ListRemoveItem(mod_s.connect_pending, &socket, intcompare);
	ListRemoveItem(mod_s.write_pending, &socket, intcompare);

	if (ListRemoveItem(mod_s.clientsds, &socket, intcompare))
		Log(TRACE_MIN, -1, "Removed socket %d", socket);
	else
	{
		Log(LOG_ERROR, -1, "Failed to remove socket %d", socket);
		rc = SOCKET_ERROR;
		goto exit;
	}
	if (socket + 1 >= mod_s.maxfdp1)
	{
		/* now we have to reset mod_s.maxfdp1 */
		ListElement* cur_clientsds = NULL;

		mod_s.maxfdp1 = 0;
		while (ListNextElement(mod_s.clientsds, &cur_clientsds))
			mod_s.maxfdp1 = max(*((int*)(cur_clientsds->content)), mod_s.maxfdp1);
		++(mod_s.maxfdp1);
		Log(TRACE_MAX, -1, "Reset max fdp1 to %d", mod_s.maxfdp1);
	}
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}
#else
/**
 *  Close a socket and remove it from the select list.
 *  @param socket the socket to close
 *  @return completion code
 */
int Socket_close(SOCKET socket)
{
	struct pollfd* fd;
	int rc = 0;

	FUNC_ENTRY;
	Socket_close_only(socket);
	Socket_abortWrite(socket);
	SocketBuffer_cleanup(socket);
	ListRemoveItem(mod_s.connect_pending, &socket, intcompare);
	ListRemoveItem(mod_s.write_pending, &socket, intcompare);

	if (mod_s.nfds == 0)
		goto exit;

	fd = bsearch(&socket, mod_s.fds_read, (size_t)mod_s.nfds, sizeof(mod_s.fds_read[0]), cmpsockfds);
	if (fd)
	{
		struct pollfd* last_fd = &mod_s.fds_read[mod_s.nfds - 1];

		if (--mod_s.nfds == 0)
		{
			free(mod_s.fds_read);
			mod_s.fds_read = NULL;
		}
		else
		{
			if (fd != last_fd)
			{
				/* shift array to remove the socket in question */
				memmove(fd, fd + 1, (mod_s.nfds - (fd - mod_s.fds_read)) * sizeof(mod_s.fds_read[0]));
			}
			mod_s.fds_read = realloc(mod_s.fds_read, sizeof(mod_s.fds_read[0]) * mod_s.nfds);
			if (mod_s.fds_read == NULL)
			{
				rc = PAHO_MEMORY_ERROR;
				goto exit;
			}
		}
		Log(TRACE_MIN, -1, "Removed socket %d", socket);
	}
	else
		Log(LOG_ERROR, -1, "Failed to remove socket %d", socket);

	fd = bsearch(&socket, mod_s.fds_write, (size_t)(mod_s.nfds+1), sizeof(mod_s.fds_write[0]), cmpsockfds);
	if (fd)
	{
		struct pollfd* last_fd = &mod_s.fds_write[mod_s.nfds];

		if (mod_s.nfds == 0)
		{
			free(mod_s.fds_write);
			mod_s.fds_write = NULL;
		}
		else
		{
			if (fd != last_fd)
			{
				/* shift array to remove the socket in question */
				memmove(fd, fd + 1, (mod_s.nfds - (fd - mod_s.fds_write)) * sizeof(mod_s.fds_write[0]));
			}
			mod_s.fds_write = realloc(mod_s.fds_write, sizeof(mod_s.fds_write[0]) * mod_s.nfds);
			if (mod_s.fds_write == NULL)
			{
				rc = PAHO_MEMORY_ERROR;
				goto exit;
			}
		}
		Log(TRACE_MIN, -1, "Removed socket %d", socket);
	}
	else
		Log(LOG_ERROR, -1, "Failed to remove socket %d", socket);
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}
#endif


/**
 *  Create a new socket and TCP connect to an address/port
 *  @param addr the address string
 *  @param port the TCP port
 *  @param sock returns the new socket
 *  @param timeout the timeout in milliseconds
 *  @return completion code 0=good, SOCKET_ERROR=fail
 */
#if defined(__GNUC__) && defined(__linux__)
int Socket_new(const char* addr, size_t addr_len, int port, SOCKET* sock, long timeout)
#else
int Socket_new(const char* addr, size_t addr_len, int port, SOCKET* sock)
#endif
{
	int type = SOCK_STREAM;
	char *addr_mem;
	struct sockaddr_in address;
#if defined(AF_INET6)
	struct sockaddr_in6 address6;
#endif
	int rc = SOCKET_ERROR;
#if defined(_WIN32) || defined(_WIN64)
	short family;
#else
	sa_family_t family = AF_INET;
#endif
	struct addrinfo *result = NULL;
	struct addrinfo hints = {0, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL, NULL};

	FUNC_ENTRY;
	*sock = SOCKET_ERROR;
	memset(&address6, '\0', sizeof(address6));

	if (addr[0] == '[')
	{
		++addr;
		--addr_len;
	}

	if ((addr_mem = malloc( addr_len + 1u )) == NULL)
	{
		rc = PAHO_MEMORY_ERROR;
		goto exit;
	}
	memcpy( addr_mem, addr, addr_len );
	addr_mem[addr_len] = '\0';

#if 0 /*defined(__GNUC__) && defined(__linux__)*/
	/* Commented out because the CI tests get intermittent ECONNABORTED return values
	 * and I don't know why yet.
	 */
	/* set getaddrinfo timeout if available */
	struct gaicb ar = {addr_mem, NULL, &hints, NULL};
	struct gaicb *reqs[] = {&ar};

	unsigned long int seconds = timeout / 1000L;
	unsigned long int nanos = (timeout - (seconds * 1000L)) * 1000000L;
	struct timespec timeoutspec = {seconds, nanos};

	rc = getaddrinfo_a(GAI_NOWAIT, reqs, 1, NULL);
	if (rc == 0)
		rc = gai_suspend((const struct gaicb* const *) reqs, 1, &timeoutspec);

	if (rc == 0)
	{
		rc = gai_error(reqs[0]);
		result = ar.ar_result;
	}
#else
	rc = getaddrinfo(addr_mem, NULL, &hints, &result);
#endif

	if (rc == 0)
	{
		struct addrinfo* res = result;

		while (res)
		{	/* prefer ip4 addresses */
			if (res->ai_family == AF_INET || res->ai_next == NULL)
				break;
			res = res->ai_next;
		}

		if (res == NULL)
			rc = SOCKET_ERROR;
		else
#if defined(AF_INET6)
		if (res->ai_family == AF_INET6)
		{
			address6.sin6_port = htons(port);
			address6.sin6_family = family = AF_INET6;
			memcpy(&address6.sin6_addr, &((struct sockaddr_in6*)(res->ai_addr))->sin6_addr, sizeof(address6.sin6_addr));
		}
		else
#endif
		if (res->ai_family == AF_INET)
		{
			memset(&address.sin_zero, 0, sizeof(address.sin_zero));
			address.sin_port = htons(port);
			address.sin_family = family = AF_INET;
			address.sin_addr = ((struct sockaddr_in*)(res->ai_addr))->sin_addr;
		}
		else
			rc = SOCKET_ERROR;

		freeaddrinfo(result);
	}
	else
	{
		Log(LOG_ERROR, -1, "getaddrinfo failed for addr %s with rc %d", addr_mem, rc);
		rc = SOCKET_ERROR;
	}

	if (rc != 0)
		Log(LOG_ERROR, -1, "%s is not a valid IP address", addr_mem);
	else
	{
		*sock =	socket(family, type, 0);
		if (*sock == INVALID_SOCKET)
			rc = Socket_error("socket", *sock);
		else
		{
#if defined(NOSIGPIPE)
			int opt = 1;

			if (setsockopt(*sock, SOL_SOCKET, SO_NOSIGPIPE, (void*)&opt, sizeof(opt)) != 0)
				Log(LOG_ERROR, -1, "Could not set SO_NOSIGPIPE for socket %d", *sock);
#endif
/*#define SMALL_TCP_BUFFER_TESTING
  This section sets the TCP send buffer to a small amount to provoke TCPSOCKET_INTERRUPTED
	return codes from send, for testing only!
*/
#if defined(SMALL_TCP_BUFFER_TESTING)
        if (1)
				{
					int optsend = 100; //2 * 1440;
					printf("Setting optsend to %d\n", optsend);
					if (setsockopt(*sock, SOL_SOCKET, SO_SNDBUF, (void*)&optsend, sizeof(optsend)) != 0)
						Log(LOG_ERROR, -1, "Could not set SO_SNDBUF for socket %d", *sock);
				}
#endif
			Log(TRACE_MIN, -1, "New socket %d for %s, port %d",	*sock, addr, port);
			if (Socket_addSocket(*sock) == SOCKET_ERROR)
				rc = Socket_error("addSocket", *sock);
			else
			{
				/* this could complete immediately, even though we are non-blocking */
				if (family == AF_INET)
					rc = connect(*sock, (struct sockaddr*)&address, sizeof(address));
	#if defined(AF_INET6)
				else
					rc = connect(*sock, (struct sockaddr*)&address6, sizeof(address6));
	#endif
				if (rc == SOCKET_ERROR)
					rc = Socket_error("connect", *sock);
				if (rc == EINPROGRESS || rc == EWOULDBLOCK)
				{
					SOCKET* pnewSd = (SOCKET*)malloc(sizeof(SOCKET));
					ListElement* result = NULL;

					if (!pnewSd)
					{
						rc = PAHO_MEMORY_ERROR;
						goto exit;
					}
					*pnewSd = *sock;
					Thread_lock_mutex(socket_mutex);
					result = ListAppend(mod_s.connect_pending, pnewSd, sizeof(SOCKET));
					Thread_unlock_mutex(socket_mutex);
					if (!result)
					{
						free(pnewSd);
						rc = PAHO_MEMORY_ERROR;
						goto exit;
					}
					Log(TRACE_MIN, 15, "Connect pending");
				}
			}
            /* Prevent socket leak by closing unusable sockets,
               as reported in https://github.com/eclipse/paho.mqtt.c/issues/135 */
            if (rc != 0 && (rc != EINPROGRESS) && (rc != EWOULDBLOCK))
            {
				Thread_lock_mutex(socket_mutex);
            	Socket_close(*sock); /* close socket and remove from our list of sockets */
				Thread_unlock_mutex(socket_mutex);
                *sock = SOCKET_ERROR; /* as initialized before */
            }
		}
	}

exit:
	if (addr_mem)
		free(addr_mem);

	FUNC_EXIT_RC(rc);
	return rc;
}

static Socket_writeContinue* writecontinue = NULL;

void Socket_setWriteContinueCallback(Socket_writeContinue* mywritecontinue)
{
	writecontinue = mywritecontinue;
}

static Socket_writeComplete* writecomplete = NULL;

void Socket_setWriteCompleteCallback(Socket_writeComplete* mywritecomplete)
{
	writecomplete = mywritecomplete;
}

static Socket_writeAvailable* writeAvailable = NULL;

void Socket_setWriteAvailableCallback(Socket_writeAvailable* mywriteavailable)
{
	writeAvailable = mywriteavailable;
}

/**
 *  Continue an outstanding write for a particular socket
 *  @param socket that socket
 *  @return completion code: 0=incomplete, 1=complete, -1=socket error
 */
int Socket_continueWrite(SOCKET socket)
{
	int rc = 0;
	pending_writes* pw;
	unsigned long curbuflen = 0L, /* cumulative total of buffer lengths */
		bytes = 0L;
	int curbuf = -1, i;
	iobuf iovecs1[5];

	FUNC_ENTRY;
	pw = SocketBuffer_getWrite(socket);

#if defined(OPENSSL)
	if (pw->ssl)
	{
		rc = SSLSocket_continueWrite(pw);
		goto exit;
	}
#endif

	for (i = 0; i < pw->count; ++i)
	{
		if (pw->bytes <= curbuflen)
		{ /* if previously written length is less than the buffer we are currently looking at,
				add the whole buffer */
			iovecs1[++curbuf].iov_len = pw->iovecs[i].iov_len;
			iovecs1[curbuf].iov_base = pw->iovecs[i].iov_base;
		}
		else if (pw->bytes < curbuflen + pw->iovecs[i].iov_len)
		{ /* if previously written length is in the middle of the buffer we are currently looking at,
				add some of the buffer */
			size_t offset = pw->bytes - curbuflen;
			iovecs1[++curbuf].iov_len = pw->iovecs[i].iov_len - (ULONG)offset;
			iovecs1[curbuf].iov_base = (char*)pw->iovecs[i].iov_base + offset;
		}
		curbuflen += pw->iovecs[i].iov_len;
	}

	if ((rc = Socket_writev(socket, iovecs1, curbuf+1, &bytes)) != SOCKET_ERROR)
	{
		pw->bytes += bytes;
		if ((rc = (pw->bytes == pw->total)))
		{  /* topic and payload buffers are freed elsewhere, when all references to them have been removed */
			for (i = 0; i < pw->count; i++)
			{
				if (pw->frees[i])
                {
					free(pw->iovecs[i].iov_base);
                    pw->iovecs[i].iov_base = NULL;
                }
			}
			rc = 1; /* signal complete */
			Log(TRACE_MIN, -1, "ContinueWrite: partial write now complete for socket %d", socket);
		}
		else
		{
			rc = 0; /* signal not complete */
			Log(TRACE_MIN, -1, "ContinueWrite wrote +%lu bytes on socket %d", bytes, socket);
		}
	}
	else /* if we got SOCKET_ERROR we need to clean up anyway - a partial write is no good anymore */
	{
		for (i = 0; i < pw->count; i++)
		{
			if (pw->frees[i])
            {
				free(pw->iovecs[i].iov_base);
                pw->iovecs[i].iov_base = NULL;
            }
		}
	}
#if defined(OPENSSL)
exit:
#endif
	FUNC_EXIT_RC(rc);
	return rc;
}



/**
 *  Continue an outstanding write for a particular socket
 *  @param socket that socket
 *  @return completion code: 0=incomplete, 1=complete, -1=socket error
 */
int Socket_abortWrite(SOCKET socket)
{
	int i = -1, rc = 0;
	pending_writes* pw;

	FUNC_ENTRY;
	if ((pw = SocketBuffer_getWrite(socket)) == NULL)
	  goto exit;

#if defined(OPENSSL)
	if (pw->ssl)
	{
		rc = SSLSocket_abortWrite(pw);
		goto exit;
	}
#endif

	for (i = 0; i < pw->count; i++)
	{
		if (pw->frees[i])
		{
			Log(TRACE_MIN, -1, "Cleaning in abortWrite for socket %d", socket);
			free(pw->iovecs[i].iov_base);
		}
	}
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}


#if defined(USE_SELECT)
/**
 *  Continue any outstanding writes for a socket set
 *  @param pwset the set of sockets
 *  @param sock in case of a socket error contains the affected socket
 *  @return completion code, 0 or SOCKET_ERROR
 */
int Socket_continueWrites(fd_set* pwset, SOCKET* sock, mutex_type mutex)
#else
/**
 *  Continue any outstanding socket writes
 
 *  @param sock in case of a socket error contains the affected socket
 *  @return completion code, 0 or SOCKET_ERROR
 */
int Socket_continueWrites(SOCKET* sock, mutex_type mutex)
#endif
{
	int rc1 = 0;
	ListElement* curpending = mod_s.write_pending->first;

	FUNC_ENTRY;
	while (curpending && curpending->content)
	{
		int socket = *(int*)(curpending->content);
		int rc = 0;
#if defined(USE_SELECT)

		if (FD_ISSET(socket, pwset) && ((rc = Socket_continueWrite(socket)) != 0))
#else
		struct pollfd* fd;

		/* find the socket in the fds structure */
		fd = bsearch(&socket, mod_s.saved.fds_write, (size_t)mod_s.saved.nfds, sizeof(mod_s.saved.fds_write[0]), cmpsockfds);

		if ((fd->revents & POLLOUT) && ((rc = Socket_continueWrite(socket)) != 0))
#endif
		{
			if (!SocketBuffer_writeComplete(socket))
				Log(LOG_SEVERE, -1, "Failed to remove pending write from socket buffer list");
#if defined(USE_SELECT)
			FD_CLR(socket, &(mod_s.pending_wset));
#endif
			if (!ListRemove(mod_s.write_pending, curpending->content))
			{
				Log(LOG_SEVERE, -1, "Failed to remove pending write from list");
				ListNextElement(mod_s.write_pending, &curpending);
			}
			curpending = mod_s.write_pending->current;

			if (writeAvailable && rc > 0)
				(*writeAvailable)(socket);

			if (writecomplete)
			{
				Thread_unlock_mutex(mutex);
				(*writecomplete)(socket, rc);
				Thread_lock_mutex(mutex);
			}
		}
		else
			ListNextElement(mod_s.write_pending, &curpending);

		if (writecontinue && rc == 0)
			(*writecontinue)(socket);

		if (rc == SOCKET_ERROR)
		{
			*sock = socket;
			rc1 = SOCKET_ERROR;
		}
	}
	FUNC_EXIT_RC(rc1);
	return rc1;
}


/**
 *  Convert a numeric address to character string
 *  @param sa	socket numerical address
 *  @param sock socket
 *  @return the peer information
 */
char* Socket_getaddrname(struct sockaddr* sa, SOCKET sock)
{
/**
 * maximum length of the address string
 */
#define ADDRLEN INET6_ADDRSTRLEN+1
/**
 * maximum length of the port string
 */
#define PORTLEN 10
	static char addr_string[ADDRLEN + PORTLEN];

#if defined(_WIN32) || defined(_WIN64)
	int buflen = ADDRLEN*2;
	wchar_t buf[ADDRLEN*2];
	if (WSAAddressToStringW(sa, sizeof(struct sockaddr_in6), NULL, buf, (LPDWORD)&buflen) == SOCKET_ERROR)
		Socket_error("WSAAddressToString", sock);
	else
		wcstombs(addr_string, buf, sizeof(addr_string));
	/* TODO: append the port information - format: [00:00:00::]:port */
	/* strcpy(&addr_string[strlen(addr_string)], "what?"); */
#else
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	size_t buflen = sizeof(addr_string) - strlen(addr_string);

	inet_ntop(sin->sin_family, &sin->sin_addr, addr_string, ADDRLEN);
	if (snprintf(&addr_string[strlen(addr_string)], buflen, ":%d", ntohs(sin->sin_port)) >= buflen)
		addr_string[sizeof(addr_string)-1] = '\0'; /* just in case of snprintf buffer filling */
#endif
	return addr_string;
}


/**
 *  Get information about the other end connected to a socket
 *  @param sock the socket to inquire on
 *  @return the peer information
 */
char* Socket_getpeer(SOCKET sock)
{
	struct sockaddr_in6 sa;
	socklen_t sal = sizeof(sa);

	if (getpeername(sock, (struct sockaddr*)&sa, &sal) == SOCKET_ERROR)
	{
		Socket_error("getpeername", sock);
		return "unknown";
	}

	return Socket_getaddrname((struct sockaddr*)&sa, sock);
}


#if defined(Socket_TEST)

int main(int argc, char *argv[])
{
	Socket_connect("127.0.0.1", 1883);
	Socket_connect("localhost", 1883);
	Socket_connect("loadsadsacalhost", 1883);
}

#endif
