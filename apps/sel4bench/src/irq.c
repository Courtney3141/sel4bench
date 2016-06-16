/*
 * Copyright 2016, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(NICTA_GPL)
 */
#include <autoconf.h>

#include "benchmark.h"
#include "processing.h"
#include "printing.h"

#include <irq.h>
#include <sel4platsupport/device.h>
#include <sel4platsupport/timer.h>
#include <sel4platsupport/plat/timer.h>
#include <stdio.h>
#include <utils/time.h>

#define TRACE_POINT_OVERHEAD 0
#define TRACE_POINT_IRQ_PATH_START 1
#define TRACE_POINT_IRQ_PATH_END 2

#ifndef CONFIG_MAX_NUM_TRACE_POINTS
#define CONFIG_MAX_NUM_TRACE_POINTS 0
#endif

static ccnt_t kernel_log_data[KERNEL_MAX_NUM_LOG_ENTRIES];
static unsigned int offsets[CONFIG_MAX_NUM_TRACE_POINTS];
static unsigned int sizes[CONFIG_MAX_NUM_TRACE_POINTS];

static void 
process(void *results) {
     irq_results_t *irq_results = (irq_results_t *) results;

    /* Sort and group data by tracepoints. A stable sort is used so the first N_IGNORED
     * results of each tracepoint can be ignored, as this keeps the data in chronological
     * order.
     */
    logging_stable_sort_log(irq_results->kernel_log, irq_results->n);
    logging_group_log_by_key(irq_results->kernel_log, irq_results->n, sizes, offsets, CONFIG_MAX_NUM_TRACE_POINTS);

    /* Copy the cycle counts into a separate array to simplify further processing */
    for (int i = 0; i < irq_results->n; ++i) {
        kernel_log_data[i] = kernel_logging_entry_get_data(&irq_results->kernel_log[i]);
    }

    /* Process log entries generated by an "empty" tracepoint, which recorded
     * the number of cycles between starting a tracepoint and stopping it
     * immediately afterwards. This will determine the overhead introduced by
     * using tracepoints.
     */
    int n_overhead_data = sizes[TRACE_POINT_OVERHEAD] - N_IGNORED;
    if (n_overhead_data <= 0) {
        ZF_LOGF("Insufficient data recorded. Was the kernel built with the relevant tracepoints?\n");
    }

    ccnt_t *overhead_data = &kernel_log_data[offsets[TRACE_POINT_OVERHEAD] + N_IGNORED];
    result_t overhead_result = process_result(overhead_data, n_overhead_data, NULL);

    /* The results of the IRQ path benchmark are split over multiple tracepoints.
     * A new buffer is allocated to store the amalgamated results. */
    int n_data = sizes[TRACE_POINT_IRQ_PATH_START] - N_IGNORED;
    if (n_data <= 0) {
        ZF_LOGF("Insufficient data recorded. Was the kernel built with the relevant tracepoints?\n");
    }

    ccnt_t *data = (ccnt_t*)malloc(sizeof(ccnt_t) * n_data);
    if (data == NULL) {
        ZF_LOGF("Failed to allocate memory\n");
    }

    /* Add the results from the IRQ path tracepoints to get the total IRQ path cycle counts.
     * The average overhead is subtracted from each cycle count (doubled as there are 2
     * tracepoints) to account for overhead added to the cycle counts by use of tracepoints.
     */
    ccnt_t *starts = &kernel_log_data[offsets[TRACE_POINT_IRQ_PATH_START] + N_IGNORED];
    ccnt_t *ends = &kernel_log_data[offsets[TRACE_POINT_IRQ_PATH_END] + N_IGNORED];
    for (int i = 0; i < n_data; ++i) {
        data[i] = starts[i] + ends[i] - (overhead_result.mean * 2);
    }

    print_banner("Tracepoint Overhead", n_overhead_data);
    result_t result = process_result(overhead_data, n_overhead_data, NULL);
    print_result_header();
    print_result(&result);
    printf("\n");

    print_banner("IRQ Path Cycle Count (accounting for overhead)", n_data);
    
    result = process_result(data, n_data, NULL);
    print_result_header();
    print_result(&result);
    printf("\n");

    free(data);
}


static benchmark_t irq_benchmark = {
    .name = "irq",
    .enabled = config_set(CONFIG_APP_IRQBENCH) && CONFIG_MAX_NUM_TRACE_POINTS == 3,
    .results_pages = BYTES_TO_SIZE_BITS_PAGES(sizeof(irq_results_t), seL4_PageBits),
    .process = process,
    .init = blank_init
};

benchmark_t *
irq_benchmark_new(void)
{
    return &irq_benchmark;
}

static void 
irquser_process(void *results) {
    irquser_results_t *raw_results;
    result_t interas_result;
    result_t intraas_result;
    result_t overhead;

    raw_results = (irquser_results_t *) results;

    overhead = process_result_ignored(raw_results->overheads, N_RUNS, N_IGNORED, "overhead");
  
    /* account for overhead */
    for (int i = 0; i < N_RUNS; i++) {
        raw_results->thread_results[i] -= overhead.min;
        raw_results->process_results[i] -= overhead.min;
    }

    intraas_result = process_result_ignored(raw_results->thread_results, N_RUNS, N_IGNORED, "thread irq");
    interas_result = process_result_ignored(raw_results->process_results, N_RUNS, N_IGNORED, "process irq");

    print_banner("IRQ Path Cycle Count (measured from user level)", N_RUNS - N_IGNORED);
    printf("Type\t");
    print_result_header();
    printf("Measurement overhead\t");
    print_result(&overhead);
    printf("Without context switch\t");
    print_result(&intraas_result);
    printf("With context switch\t");
    print_result(&interas_result);
}

static benchmark_t irquser_benchmark = {
    .name = "irquser",
    .enabled = config_set(CONFIG_APP_IRQUSERBENCH),
    .results_pages = BYTES_TO_SIZE_BITS_PAGES(sizeof(irquser_results_t), seL4_PageBits),
    .process = irquser_process,
    .init = blank_init
};

benchmark_t *
irquser_benchmark_new(void) 
{
   return &irquser_benchmark;
}

