#include <rtthread.h>
#include <lwip/sockets.h>

/* һ������buffer��ʵ�� */
#define BUFFER_SIZE		256
struct rb
{
	rt_uint16_t read_index, write_index;
	rt_uint8_t *buffer_ptr;
	rt_uint16_t buffer_size;
};

/* ��ʼ������buffer��sizeָ����buffer�Ĵ�С��ע�����ﲢû�����ݵ�ַ���������� */
static void rb_init(struct rb* rb, rt_uint8_t *pool, rt_uint16_t size)
{
	RT_ASSERT(rb != RT_NULL);

	/* �Զ�дָ������*/
	rb->read_index = rb->write_index = 0;

	/* ����buffer���ڴ����ݿ� */
	rb->buffer_ptr = pool;
	rb->buffer_size = size;
}

/* ����buffer��д������ */
static rt_bool_t rb_put(struct rb* rb, const rt_uint8_t *ptr, rt_uint16_t length)
{
	rt_size_t size;

	/* �ж��Ƿ����㹻��ʣ��ռ� */
	if (rb->read_index > rb->write_index)
		size = rb->read_index - rb->write_index;
	else
		size = rb->buffer_size - rb->write_index + rb->read_index;

	/* û�ж���Ŀռ� */
	if (size < length) return RT_FALSE;

	if (rb->read_index > rb->write_index)
	{
		/* read_index - write_index ��Ϊ�ܵĿ���ռ� */
		memcpy(&rb->buffer_ptr[rb->write_index], ptr, length);
		rb->write_index += length;
	}
	else
	{
		if (rb->buffer_size - rb->write_index > length)
		{
			/* write_index ����ʣ��Ŀռ����㹻�ĳ��� */
			memcpy(&rb->buffer_ptr[rb->write_index], ptr, length);
			rb->write_index += length;
		}
		else
		{
			/*
			 * write_index ����ʣ��Ŀռ䲻�����㹻�ĳ��ȣ���Ҫ�Ѳ������ݸ��Ƶ�
			 * ǰ���ʣ��ռ���
			 */
			memcpy(&rb->buffer_ptr[rb->write_index], ptr,
				   rb->buffer_size - rb->write_index);
			memcpy(&rb->buffer_ptr[0], &ptr[rb->buffer_size - rb->write_index],
				   length - (rb->buffer_size - rb->write_index));
			rb->write_index = length - (rb->buffer_size - rb->write_index);
		}
	}

	return RT_TRUE;
}

/* �ӻ���buffer�ж������� */
static rt_bool_t rb_get(struct rb* rb, rt_uint8_t *ptr, rt_uint16_t length)
{
	rt_size_t size;

	/* �ж��Ƿ����㹻������ */
	if (rb->read_index > rb->write_index)
		size = rb->buffer_size - rb->read_index + rb->write_index;
	else
		size = rb->write_index - rb->read_index;

	/* û���㹻������ */
	if (size < length) return RT_FALSE;

	if (rb->read_index > rb->write_index)
	{
		if (rb->buffer_size - rb->read_index > length)
		{
			/* read_index�������㹻�ֱ࣬�Ӹ��� */
			memcpy(ptr, &rb->buffer_ptr[rb->read_index], length);
			rb->read_index += length;
		}
		else
		{
			/* read_index�����ݲ�������Ҫ�ֶθ��� */
			memcpy(ptr, &rb->buffer_ptr[rb->read_index],
				   rb->buffer_size - rb->read_index);
			memcpy(&ptr[rb->buffer_size - rb->read_index], &rb->buffer_ptr[0],
				   length - rb->buffer_size + rb->read_index);
			rb->read_index = length - rb->buffer_size + rb->read_index;
		}
	}
	else
	{
		/*
		 * read_indexҪ��write_indexС���ܵ�����������ǰ���Ѿ���������������
		 * �ϣ���ֱ�Ӹ��Ƴ����ݡ�
		 */
		memcpy(ptr, &rb->buffer_ptr[rb->read_index], length);
		rb->read_index += length;
	}

	return RT_TRUE;
}

#define TELNET_PORT	23
struct telnet_session
{
	struct rb rx_ringbuffer;
	struct rb tx_ringbuffer;
};

/* RT-Thread Device Driver Interface */
static rt_err_t telnet_init(rt_device_t dev)
{
	return RT_EOK;
}

static rt_err_t telnet_open(rt_device_t dev, rt_uint16_t oflag)
{
	return RT_EOK;
}

static rt_err_t telnet_close(rt_device_t dev)
{
	return RT_EOK;
}

static rt_size_t telnet_read(rt_device_t dev, rt_off_t pos, void* buffer, rt_size_t size)
{
	/* read from rx ring buffer */
	return rb_get(&(telnet_srv->rx_ringbuffer), buffer, size);
}

static rt_size_t telnet_write (rt_device_t dev, rt_off_t pos, const void* buffer, rt_size_t size)
{
	/* send to network side */
	return size;
}

static rt_err_t telnet_control(rt_device_t dev, rt_uint8_t cmd, void *args)
{
	return RT_EOK;
}

void telnet_task(void* parameter)
{
	rt_uint8_t recv_data[32];
	rt_uint32_t sin_size;
	int sock, connected, bytes_received;
	struct sockaddr_in server_addr, client_addr;

	/* create TCP socket */
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		/* create failed */
		rt_kprintf("Socket error\n");

		/* release the memory */
		rt_free(recv_data);
		return;
	}

	/* init server address */
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(TELNET_PORT); /* telnet srv port */
	server_addr.sin_addr.s_addr = INADDR_ANY;
	rt_memset(&(server_addr.sin_zero),8, sizeof(server_addr.sin_zero));

	/* bind socket */
	if (bind(sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1)
	{
		/* failed */
		rt_kprintf("Unable to bind\n");

		/* release the memory */
		rt_free(recv_data);
		return;
	}

	/* listen on socket */
	if (listen(sock, 5) == -1)
	{
		rt_kprintf("Listen error\n");

		/* release recv buffer */
		rt_free(recv_data);
		return;
	}

	rt_kprintf("\nTelnet Server Waiting for client on port 23...\n");
	while(1)
	{
		sin_size = sizeof(struct sockaddr_in);

		/* accept a client */
		connected = accept(sock, (struct sockaddr *)&client_addr, &sin_size);

		/* handle client */
		while (1)
		{
			/* receive data from client */
			bytes_received = recv(connected,recv_data, 1024, 0);
			if (bytes_received < 0)
			{
				/* failed, close this connection. */
				lwip_close(connected);
				break;
			}

			/* get message */
			recv_data[bytes_received] = '\0';

			/* notify to shell */
		}
	}

	/* close lwip */
	lwip_close(sock);

	/* release memory */
	rt_free(recv_data);

	return ;
}

void telnet_srv()
{
	rt_thread_t tid;

	tid = rt_thread_create("telnet", telnet_task, RT_NULL,
						   2048, 0x25, 5);
	if (tid != RT_NULL)
		rt_thread_startup(tid);
}

#ifdef RT_USING_FINSH
#include <finsh.h>
FINSH_FUNCTION_EXPORT(telnet_srv, startup telnet server);
#endif
