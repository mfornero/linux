/*
 * XILINX AXI DMA Engine test module
 *
 * Copyright (C) 2010 Xilinx, Inc. All rights reserved.
 *
 * Based on Atmel DMA Test Client
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/dma/xilinx_dma.h>
#include <linux/semaphore.h>

static unsigned int test_buf_size = 64;
module_param(test_buf_size, uint, S_IRUGO);
MODULE_PARM_DESC(test_buf_size, "Size of the memcpy test buffer");

static unsigned int iterations = 1;
module_param(iterations, uint, S_IRUGO);
MODULE_PARM_DESC(iterations,
		"Iterations before stopping test (default: infinite)");

/*
 * Initialization patterns. All bytes in the source buffer has bit 7
 * set, all bytes in the destination buffer has bit 7 cleared.
 *
 * Bit 6 is set for all bytes which are to be copied by the DMA
 * engine. Bit 5 is set for all bytes which are to be overwritten by
 * the DMA engine.
 *
 * The remaining bits are the inverse of a counter which increments by
 * one for each byte address.
 */
#define PATTERN_SRC		0x80
#define PATTERN_DST		0x00
#define PATTERN_COPY		0x40
#define PATTERN_OVERWRITE	0x20
#define PATTERN_COUNT_MASK	0x1f
#define PATTERN_HEADER(buf_idx) (1 << (buf_idx + 16))
#define PATTERN_HEADER_SIZE 4

struct dmatest_slave_thread {
	struct list_head node;
	struct task_struct *task;
	struct dma_chan *tx_chan;
	struct dma_chan *rx_chan;
	u8 **srcs;
	u8 **dsts;
	enum dma_transaction_type type;
	bool done;
};

struct dmatest_chan {
	struct list_head node;
	struct dma_chan *chan;
	struct list_head threads;
};

/*
 * These are protected by dma_list_mutex since they're only used by
 * the DMA filter function callback
 */
static DECLARE_WAIT_QUEUE_HEAD(thread_wait);
static LIST_HEAD(dmatest_channels);
static unsigned int nr_channels;

static bool is_threaded_test_run(struct dmatest_chan *tx_dtc,
					struct dmatest_chan *rx_dtc)
{
	struct dmatest_slave_thread *thread;
	int ret = false;

	list_for_each_entry(thread, &tx_dtc->threads, node) {
		if (!thread->done)
			ret = true;
	}

	list_for_each_entry(thread, &rx_dtc->threads, node) {
		if (!thread->done)
			ret = true;
	}
	return ret;
}

static unsigned long dmatest_random(void)
{
	unsigned long buf;

	get_random_bytes(&buf, sizeof(buf));
	return buf;
}

static void dmatest_init_srcs(u8 **bufs, unsigned int start, unsigned int len)
{
	unsigned int i;
	unsigned int buf_cnt = 0;
	u8 *buf;
	u32 *buf32;

	for (; (buf = *bufs); bufs++) {
		for (i = 0; i < start; i++)
			buf[i] = PATTERN_SRC | (~i & PATTERN_COUNT_MASK);
		/* Mark the start of the frame */
		buf32 = (u32*)&buf[i];
		*buf32 = PATTERN_HEADER(buf_cnt);
		i+=PATTERN_HEADER_SIZE;
		for ( ; i < start + len; i++)
			buf[i] = PATTERN_SRC | PATTERN_COPY
				| (~i & PATTERN_COUNT_MASK);
		for ( ; i < test_buf_size; i++)
			buf[i] = PATTERN_SRC | (~i & PATTERN_COUNT_MASK);
		buf++;
		buf_cnt++;
	}
}

static void dmatest_init_dsts(u8 **bufs, unsigned int start, unsigned int len)
{
	unsigned int i;
	u8 *buf;

	for (; (buf = *bufs); bufs++) {
		for (i = 0; i < start; i++)
			buf[i] = PATTERN_DST | (~i & PATTERN_COUNT_MASK);
		for ( ; i < start + len; i++)
			buf[i] = PATTERN_DST | PATTERN_OVERWRITE
				| (~i & PATTERN_COUNT_MASK);
		for ( ; i < test_buf_size; i++)
			buf[i] = PATTERN_DST | (~i & PATTERN_COUNT_MASK);
	}
}

static void dmatest_mismatch(u8 actual, u8 pattern, unsigned int index,
		unsigned int counter, bool is_srcbuf)
{
	u8 diff = actual ^ pattern;
	u8 expected = pattern | (~counter & PATTERN_COUNT_MASK);
	const char *thread_name = current->comm;

	if (is_srcbuf)
		pr_warn(
		"%s: srcbuf[0x%x] overwritten! Expected %02x, got %02x\n",
				thread_name, index, expected, actual);
	else if ((pattern & PATTERN_COPY)
			&& (diff & (PATTERN_COPY | PATTERN_OVERWRITE)))
		pr_warn(
		"%s: dstbuf[0x%x] not copied! Expected %02x, got %02x\n",
				thread_name, index, expected, actual);
	else if (diff & PATTERN_SRC)
		pr_warn(
		"%s: dstbuf[0x%x] was copied! Expected %02x, got %02x\n",
				thread_name, index, expected, actual);
	else
		pr_warn(
		"%s: dstbuf[0x%x] mismatch! Expected %02x, got %02x\n",
				thread_name, index, expected, actual);
}

static unsigned int dmatest_verify_bufheader(u8 **bufs, unsigned int start,
		bool is_srcbuf)
{
	unsigned int error_count = 0;
	u8 *buf;
	u32 *buf32;
	int buf_cnt = 0;
	const char *thread_name = current->comm;


	for (; (buf = *bufs); bufs++) {
		buf32 = (u32 *)&buf[start];
		if (*buf32 != PATTERN_HEADER(buf_cnt)){
			if (is_srcbuf)
				pr_warn("%s: srcbuf[0x%x] header mismatch! Expected %08x, got %08x\n",
						thread_name, start, PATTERN_HEADER(buf_cnt), *buf32);
			else
				pr_warn("%s: dstbuf[0x%x] header mismatch! Expected %08x, got %08x\n",
						thread_name, start, PATTERN_HEADER(buf_cnt), *buf32);
			error_count++;
		}
		buf_cnt++;
	}

	return error_count;
}

static unsigned int dmatest_verify(u8 **bufs, unsigned int start,
		unsigned int end, unsigned int counter, u8 pattern,
		bool is_srcbuf)
{
	unsigned int i;
	unsigned int error_count = 0;
	u8 actual;
	u8 expected;
	u8 *buf;
	unsigned int counter_orig = counter;

	for (; (buf = *bufs); bufs++) {
		counter = counter_orig;
		for (i = start; i < end; i++) {
			actual = buf[i];
			expected = pattern | (~counter & PATTERN_COUNT_MASK);
			if (actual != expected) {
				if (error_count < 32)
					dmatest_mismatch(actual, pattern, i,
							counter, is_srcbuf);
				error_count++;
			}
			counter++;
		}
	}

	if (error_count > 32)
		pr_warn("%s: %u errors suppressed\n",
			current->comm, error_count - 32);

	return error_count;
}

static void dmatest_slave_tx_callback(void *token)
{
	struct semaphore *sem = token;
	up(sem);
}

static void dmatest_slave_rx_callback(void *token)
{
	struct semaphore *sem = token;
	up(sem);
}

/* Function for slave transfers
 * Each thread requires 2 channels, one for transmit, and one for receive
 */
static int dmatest_slave_func(void *data)
{
	struct dmatest_slave_thread	*thread = data;
	struct dma_chan *tx_chan;
	struct dma_chan *rx_chan;
	const char *thread_name;
	unsigned int src_off, dst_off, len;
	unsigned int error_count;
	unsigned int failed_tests = 0;
	unsigned int total_tests = 0;
	dma_cookie_t *tx_cookie;
	dma_cookie_t *rx_cookie;
	enum dma_status status;
	enum dma_ctrl_flags flags;
	int ret;
	int src_cnt;
	int dst_cnt;
	int bd_cnt = 4;
	int i;
	thread_name = current->comm;

	ret = -ENOMEM;

	/* JZ: limit testing scope here */
	test_buf_size = 700;

	smp_rmb();
	tx_chan = thread->tx_chan;
	rx_chan = thread->rx_chan;
	src_cnt = dst_cnt = bd_cnt;

	thread->srcs = kcalloc(src_cnt+1, sizeof(u8 *), GFP_KERNEL);
	if (!thread->srcs)
		goto err_srcs;
	for (i = 0; i < src_cnt; i++) {
		thread->srcs[i] = kmalloc(test_buf_size, GFP_KERNEL);
		if (!thread->srcs[i])
			goto err_srcbuf;
	}
	thread->srcs[i] = NULL;

	rx_cookie = kcalloc(src_cnt, sizeof(dma_cookie_t), GFP_KERNEL);
	if(!rx_cookie)
		goto err_rxcookie;


	thread->dsts = kcalloc(dst_cnt+1, sizeof(u8 *), GFP_KERNEL);
	if (!thread->dsts)
		goto err_dsts;
	for (i = 0; i < dst_cnt; i++) {
		thread->dsts[i] = kmalloc(test_buf_size, GFP_KERNEL);
		if (!thread->dsts[i])
			goto err_dstbuf;
	}
	thread->dsts[i] = NULL;

	tx_cookie = kcalloc(dst_cnt, sizeof(dma_cookie_t), GFP_KERNEL);
	if(!tx_cookie)
		goto err_txcookie;

	set_user_nice(current, 10);

	flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

	while (!kthread_should_stop()
		&& !(iterations && total_tests >= iterations)) {
		struct dma_device *tx_dev = tx_chan->device;
		struct dma_device *rx_dev = rx_chan->device;
		struct dma_async_tx_descriptor *txd = NULL;
		struct dma_async_tx_descriptor *rxd = NULL;
		dma_addr_t dma_srcs[src_cnt];
		dma_addr_t dma_dsts[dst_cnt];
		struct semaphore rx_sem;
		struct semaphore tx_sem;
		unsigned long rx_tmo =
				msecs_to_jiffies(300000); /* RX takes longer */
		unsigned long tx_tmo = msecs_to_jiffies(30000);
		u8 align = 0;

		total_tests++;

		/* honor larger alignment restrictions */
		align = tx_dev->copy_align;
		if (rx_dev->copy_align > align)
			align = rx_dev->copy_align;

		if (1 << align > test_buf_size) {
			pr_err("%u-byte buffer too small for %d-byte alignment\n",
				test_buf_size, 1 << align);
			break;
		}

		len = 16 % test_buf_size + 1;
		len = (len >> align) << align;
		if (!len)
			len = 1 << align;
		src_off = dmatest_random() % (test_buf_size - len + 1);
		dst_off = dmatest_random() % (test_buf_size - len + 1);

		src_off = (src_off >> align) << align;
		dst_off = (dst_off >> align) << align;

		dmatest_init_srcs(thread->srcs, src_off, len);
		dmatest_init_dsts(thread->dsts, dst_off, len);

		for (i = 0; i < src_cnt; i++) {
			u8 *buf = thread->srcs[i] + src_off;

			dma_srcs[i] = dma_map_single(tx_dev->dev, buf, len,
							DMA_MEM_TO_DEV);
		}

		for (i = 0; i < dst_cnt; i++) {
			dma_dsts[i] = dma_map_single(rx_dev->dev,
							thread->dsts[i],
							test_buf_size,
							DMA_MEM_TO_DEV);

			dma_unmap_single(rx_dev->dev, dma_dsts[i],
							test_buf_size,
							DMA_MEM_TO_DEV);

			dma_dsts[i] = dma_map_single(rx_dev->dev,
							thread->dsts[i],
							test_buf_size,
							DMA_DEV_TO_MEM);
		}

		sema_init(&rx_sem, 0);
		sema_init(&tx_sem, 0);

		/* Queue Up Rx Descriptors */
		for (i = 0; i < bd_cnt; i++) {
			rxd = dmaengine_prep_slave_single(rx_chan,
					dma_dsts[i] + dst_off, len, DMA_DEV_TO_MEM, flags);
			if (!rxd) {
				for (i = 0; i < src_cnt; i++)
					dma_unmap_single(tx_dev->dev, dma_srcs[i], len,
							DMA_MEM_TO_DEV);
				for (i = 0; i < dst_cnt; i++)
					dma_unmap_single(rx_dev->dev, dma_dsts[i],
							test_buf_size,
							DMA_DEV_TO_MEM);
				pr_warn(
				"%s: #%u: prep error with src_off=0x%x ",
					thread_name, total_tests - 1, src_off);
				pr_warn("dst_off=0x%x len=0x%x\n",
						dst_off, len);
				msleep(100);
				failed_tests++;
				goto iteration_loop_end;
			}

			rxd->callback = dmatest_slave_rx_callback;
			rxd->callback_param = &rx_sem;
			rx_cookie[i] = rxd->tx_submit(rxd);

			if (dma_submit_error(rx_cookie[i])) {
				pr_warn(
				"%s: #%u: submit error %d with src_off=0x%x ",
						thread_name, total_tests - 1,
						rx_cookie[i], src_off);
				pr_warn("dst_off=0x%x len=0x%x\n",
						dst_off, len);
				msleep(100);
				failed_tests++;
				goto iteration_loop_end;
			}
			dma_async_issue_pending(rx_chan);
		}


		/*Fire off the Tx Descriptors */
		for (i = 0; i < bd_cnt; i++) {

			txd = dmaengine_prep_slave_single(tx_chan,
					dma_srcs[i], len, DMA_MEM_TO_DEV, flags);


			if (!txd) {
				for (i = 0; i < src_cnt; i++)
					dma_unmap_single(tx_dev->dev, dma_srcs[i], len,
							DMA_MEM_TO_DEV);
				for (i = 0; i < dst_cnt; i++)
					dma_unmap_single(rx_dev->dev, dma_dsts[i],
							test_buf_size,
							DMA_DEV_TO_MEM);
				pr_warn(
				"%s: #%u: prep error with src_off=0x%x ",
					thread_name, total_tests - 1, src_off);
				pr_warn("dst_off=0x%x len=0x%x\n",
						dst_off, len);
				msleep(100);
				failed_tests++;
				dmaengine_terminate_sync(rx_chan);
				goto iteration_loop_end;
			}

			txd->callback = dmatest_slave_tx_callback;
			txd->callback_param = &tx_sem;
			tx_cookie[i] = txd->tx_submit(txd);
			if (dma_submit_error(tx_cookie[i])) {
				pr_warn(
				"%s: #%u: submit error %d with src_off=0x%x ",
						thread_name, total_tests - 1,
						tx_cookie[i], src_off);
				pr_warn("dst_off=0x%x len=0x%x\n",
						dst_off, len);
				msleep(100);
				failed_tests++;
				dmaengine_terminate_sync(rx_chan);
				goto iteration_loop_end;
			}
			dma_async_issue_pending(tx_chan);

			ret = down_timeout(&tx_sem, tx_tmo);

			status = dma_async_is_tx_complete(tx_chan, tx_cookie[i],
								NULL, NULL);

			if (ret != 0) {
				pr_warn("%s: #%u: tx test timed out\n",
					   thread_name, total_tests - 1);
				failed_tests++;
				dmaengine_terminate_sync(rx_chan);
				goto iteration_loop_end;
			} else if (status != DMA_COMPLETE) {
				pr_warn(
				"%s: #%u: tx got completion callback, ",
					   thread_name, total_tests - 1);
				pr_warn("but status is \'%s\'\n",
					   status == DMA_ERROR ? "error" :
								"in progress");
				failed_tests++;
				dmaengine_terminate_sync(rx_chan);
				goto iteration_loop_end;
			}
		}
		/* Complete the Rx Descriptors */
		for (i = 0; i < bd_cnt; i++) {
			ret = down_timeout(&rx_sem, rx_tmo);
			status = dma_async_is_tx_complete(rx_chan, rx_cookie[i],
								NULL, NULL);

			if (ret != 0) {
				pr_warn("%s: #%u: rx test timed out\n",
					   thread_name, total_tests - 1);
				failed_tests++;
				goto iteration_loop_end;
			} else if (status != DMA_COMPLETE) {
				pr_warn(
				"%s: #%u: rx got completion callback, ",
					   thread_name, total_tests - 1);
				pr_warn("but status is \'%s\'\n",
					   status == DMA_ERROR ? "error" :
								"in progress");
				failed_tests++;
				goto iteration_loop_end;
			}
		}


		/* Unmap by myself */
		for (i = 0; i < dst_cnt; i++)
			dma_unmap_single(rx_dev->dev, dma_dsts[i],
					test_buf_size, DMA_DEV_TO_MEM);

		error_count = 0;

		pr_debug("%s: verifying source buffer...\n", thread_name);
		error_count += dmatest_verify(thread->srcs, 0, src_off,
				0, PATTERN_SRC, true);
		error_count += dmatest_verify_bufheader(thread->srcs, src_off, true);
		error_count += dmatest_verify(thread->srcs, src_off+PATTERN_HEADER_SIZE,
				src_off + len, src_off + PATTERN_HEADER_SIZE,
				PATTERN_SRC | PATTERN_COPY, true);
		error_count += dmatest_verify(thread->srcs, src_off + len,
				test_buf_size, src_off + len,
				PATTERN_SRC, true);

		pr_debug("%s: verifying dest buffer...\n",
				thread->task->comm);
		error_count += dmatest_verify(thread->dsts, 0, dst_off,
				0, PATTERN_DST, false);
		error_count += dmatest_verify_bufheader(thread->dsts, dst_off, false);
		error_count += dmatest_verify(thread->dsts, dst_off+PATTERN_HEADER_SIZE,
				dst_off + len, src_off + PATTERN_HEADER_SIZE,
				PATTERN_SRC | PATTERN_COPY, false);
		error_count += dmatest_verify(thread->dsts, dst_off + len,
				test_buf_size, dst_off + len,
				PATTERN_DST, false);

		if (error_count) {
			pr_warn("%s: #%u: %u errors with ",
				thread_name, total_tests - 1, error_count);
			pr_warn("src_off=0x%x dst_off=0x%x len=0x%x\n",
				src_off, dst_off, len);
			failed_tests++;
		} else {
			pr_debug("%s: #%u: No errors with ",
				thread_name, total_tests - 1);
			pr_debug("src_off=0x%x dst_off=0x%x len=0x%x\n",
				src_off, dst_off, len);
		}
iteration_loop_end:
		continue;
	}

	ret = 0;
	kfree(tx_cookie);
err_txcookie:
	for (i = 0; thread->dsts[i]; i++)
		kfree(thread->dsts[i]);
err_dstbuf:
	kfree(thread->dsts);
err_dsts:
	kfree(rx_cookie);
err_rxcookie:
	for (i = 0; thread->srcs[i]; i++)
		kfree(thread->srcs[i]);
err_srcbuf:
	kfree(thread->srcs);
err_srcs:
	pr_notice("%s: terminating after %u tests, %u failures (status %d)\n",
			thread_name, total_tests, failed_tests, ret);

	thread->done = true;
	wake_up(&thread_wait);

	return ret;
}

static void dmatest_cleanup_channel(struct dmatest_chan *dtc)
{
	struct dmatest_slave_thread *thread;
	struct dmatest_slave_thread *_thread;
	int ret;

	list_for_each_entry_safe(thread, _thread, &dtc->threads, node) {
		ret = kthread_stop(thread->task);
		pr_debug("dmatest: thread %s exited with status %d\n",
				thread->task->comm, ret);
		list_del(&thread->node);
		put_task_struct(thread->task);
		kfree(thread);
	}
	kfree(dtc);
}

static int dmatest_add_slave_threads(struct dmatest_chan *tx_dtc,
					struct dmatest_chan *rx_dtc)
{
	struct dmatest_slave_thread *thread;
	struct dma_chan *tx_chan = tx_dtc->chan;
	struct dma_chan *rx_chan = rx_dtc->chan;

	thread = kzalloc(sizeof(struct dmatest_slave_thread), GFP_KERNEL);
	if (!thread) {
		pr_warn("dmatest: No memory for slave thread %s-%s\n",
				dma_chan_name(tx_chan), dma_chan_name(rx_chan));

	}

	thread->tx_chan = tx_chan;
	thread->rx_chan = rx_chan;
	thread->type = (enum dma_transaction_type)DMA_SLAVE;
	smp_wmb();
	thread->task = kthread_run(dmatest_slave_func, thread, "%s-%s",
		dma_chan_name(tx_chan), dma_chan_name(rx_chan));
	if (IS_ERR(thread->task)) {
		pr_warn("dmatest: Failed to run thread %s-%s\n",
				dma_chan_name(tx_chan), dma_chan_name(rx_chan));
		kfree(thread);
		return PTR_ERR(thread->task);
	}

	/* srcbuf and dstbuf are allocated by the thread itself */
	get_task_struct(thread->task);
	list_add_tail(&thread->node, &tx_dtc->threads);

	/* Added one thread with 2 channels */
	return 1;
}

static int dmatest_add_slave_channels(struct dma_chan *tx_chan,
					struct dma_chan *rx_chan)
{
	struct dmatest_chan *tx_dtc;
	struct dmatest_chan *rx_dtc;
	unsigned int thread_count = 0;

	tx_dtc = kmalloc(sizeof(struct dmatest_chan), GFP_KERNEL);
	if (!tx_dtc) {
		pr_warn("dmatest: No memory for tx %s\n",
				dma_chan_name(tx_chan));
		return -ENOMEM;
	}

	rx_dtc = kmalloc(sizeof(struct dmatest_chan), GFP_KERNEL);
	if (!rx_dtc) {
		pr_warn("dmatest: No memory for rx %s\n",
				dma_chan_name(rx_chan));
		return -ENOMEM;
	}

	tx_dtc->chan = tx_chan;
	rx_dtc->chan = rx_chan;
	INIT_LIST_HEAD(&tx_dtc->threads);
	INIT_LIST_HEAD(&rx_dtc->threads);

	dmatest_add_slave_threads(tx_dtc, rx_dtc);
	thread_count += 1;

	pr_info("dmatest: Started %u threads using %s %s\n",
		thread_count, dma_chan_name(tx_chan), dma_chan_name(rx_chan));

	list_add_tail(&tx_dtc->node, &dmatest_channels);
	list_add_tail(&rx_dtc->node, &dmatest_channels);
	nr_channels += 2;

	if (iterations)
		wait_event(thread_wait, !is_threaded_test_run(tx_dtc, rx_dtc));

	return 0;
}

static int xilinx_axidmatest_probe(struct platform_device *pdev)
{
	struct dma_chan *chan, *rx_chan;
	int err;

	chan = dma_request_slave_channel(&pdev->dev, "axidma0");
	if (!chan) {
		pr_warn("xilinx_dmatest: No Tx channel\n");
		return -EPROBE_DEFER;
	}

	rx_chan = dma_request_slave_channel(&pdev->dev, "axidma1");
	if (!rx_chan) {
		err = -EPROBE_DEFER;
		pr_warn("xilinx_dmatest: No Rx channel\n");
		goto free_tx;
	}

	err = dmatest_add_slave_channels(chan, rx_chan);
	if (err) {
		pr_err("xilinx_dmatest: Unable to add channels\n");
		goto free_rx;
	}

	return 0;

free_rx:
	dma_release_channel(rx_chan);
free_tx:
	dma_release_channel(chan);

	return err;
}

static int xilinx_axidmatest_remove(struct platform_device *pdev)
{
	struct dmatest_chan *dtc, *_dtc;
	struct dma_chan *chan;

	list_for_each_entry_safe(dtc, _dtc, &dmatest_channels, node) {
		list_del(&dtc->node);
		chan = dtc->chan;
		dmatest_cleanup_channel(dtc);
		pr_info("xilinx_dmatest: dropped channel %s\n",
			dma_chan_name(chan));
		dma_release_channel(chan);
	}
	return 0;
}

static const struct of_device_id xilinx_axidmatest_of_ids[] = {
	{ .compatible = "xlnx,axi-dma-test-1.00.a",},
	{}
};

static struct platform_driver xilinx_axidmatest_driver = {
	.driver = {
		.name = "xilinx_axidmatest",
		.owner = THIS_MODULE,
		.of_match_table = xilinx_axidmatest_of_ids,
	},
	.probe = xilinx_axidmatest_probe,
	.remove = xilinx_axidmatest_remove,
};

static int __init axidma_init(void)
{
	return platform_driver_register(&xilinx_axidmatest_driver);

}
late_initcall(axidma_init);

static void __exit axidma_exit(void)
{
	platform_driver_unregister(&xilinx_axidmatest_driver);
}
module_exit(axidma_exit)

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx AXI DMA Test Client");
MODULE_LICENSE("GPL v2");
