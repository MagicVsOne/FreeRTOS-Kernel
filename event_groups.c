/*
 * FreeRTOS Kernel <DEVELOPMENT BRANCH>
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/* Standard includes. */
#include <stdlib.h>

/* Defining MPU_WRAPPERS_INCLUDED_FROM_API_FILE prevents task.h from redefining
 * all the API functions to use the MPU wrappers. That should only be done when
 * task.h is included from an application file. */
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "event_groups.h"

/* The MPU ports require MPU_WRAPPERS_INCLUDED_FROM_API_FILE to be defined
 * for the header files above, but not in this file, in order to generate the
 * correct privileged Vs unprivileged linkage and placement. */
#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE

/* This entire source file will be skipped if the application is not configured
 * to include event groups functionality. This #if is closed at the very bottom
 * of this file. If you want to include event groups then ensure
 * configUSE_EVENT_GROUPS is set to 1 in FreeRTOSConfig.h. */
// 事件组是 FreeRTOS 里用于任务间同步和通信的机制
#if ( configUSE_EVENT_GROUPS == 1 )

    typedef struct EventGroupDef_t
    {
        //变量名前缀：u：unsigned，x: 通用数据类型, BaseType_t
        //该事件组包含的每个事件对应的发生标志位：对应bit位为1：发生，为0：未发生
        EventBits_t uxEventBits;
        //等待事件列表
        List_t xTasksWaitingForBits; /**< List of tasks waiting for a bit to be set. */

        #if ( configUSE_TRACE_FACILITY == 1 )
            UBaseType_t uxEventGroupNumber;
        #endif

        #if ( ( configSUPPORT_STATIC_ALLOCATION == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
            //标志位，记录该事件组是静态分配还是动态分配的，值为 pdTRUE 时表示为静态分配（static）
            //变量名前缀：u：unsigned，c：char
            uint8_t ucStaticallyAllocated; /**< Set to pdTRUE if the event group is statically allocated to ensure no attempt is made to free the memory. */
        #endif
    } EventGroup_t;

/*-----------------------------------------------------------*/

/*
 * Test the bits set in uxCurrentEventBits to see if the wait condition is met.
 * The wait condition is defined by xWaitForAllBits.  If xWaitForAllBits is
 * pdTRUE then the wait condition is met if all the bits set in uxBitsToWaitFor
 * are also set in uxCurrentEventBits.  If xWaitForAllBits is pdFALSE then the
 * wait condition is met if any of the bits set in uxBitsToWait for are also set
 * in uxCurrentEventBits.
 */
    static BaseType_t prvTestWaitCondition( const EventBits_t uxCurrentEventBits,
                                            const EventBits_t uxBitsToWaitFor,
                                            const BaseType_t xWaitForAllBits ) PRIVILEGED_FUNCTION;

/*-----------------------------------------------------------*/

    /** 仅当设备支持静态分配内存空间时，允许调用 xEventGroupCreateStatic 函数创建静态事件组
     *  该函数传入一个类型为 StaticEventGroup_t 结构体的指针，返回 EventGroup_t 结构体类型的指针，中间涉及一个指针类型的强制转换
     *  但是两个指针指向的是同一个地址，只是对于该地址包含的内存区域块有不同的分割识别方式（结构体不同）
     *  因此，两个结构体对应的内存大小必须相等，以防止溢出等问题；同时，通过 断言（configASSERT）+ 内存大小比较 + 静检工具（注释中使用 coverity 标识）做内存大小判断
     *  通过封装设计，屏蔽了两种结构体之间的差异，两种结构体的 内存布局、类型定义、命名均可不同，只需整体大小相同
     *  这种屏蔽可以让对应模块的开发者只关心自己的模块结构定义，并能放心调用该函数注册静态事件组。（健壮性）
     *  该函数同时进行了列表初始化、静态标志位（ucStaticallyAllocated）置 1 、事件标志位清零
     *  静态（static）与动态（dynamic）分别在于，静态是事先已分配好内存，在函数上的表现为：静态创建事件组时，需传入已提前申请好的内存块作为入参，并对该内存块进行操作
     *  而下方的动态事件组创建函数 xEventGroupCreate 中，无需代表已分配好内存的入参，由系统动态分配内存
     * */
    #if ( configSUPPORT_STATIC_ALLOCATION == 1 )

        /* 已预先定义：
        * typedef struct EventGroupDef_t   * EventGroupHandle_t;
        * typedef struct EventGroupDef_t { ... } EventGroup_t;
        * */
        EventGroupHandle_t xEventGroupCreateStatic( StaticEventGroup_t * pxEventGroupBuffer )
        {
            EventGroup_t * pxEventBits;

            /* 源码中下述跟踪函数实现为空，可自行根据不同平台定义最终函数，定义在 FreeRTOS.h 
            * #ifndef traceENTER_xEventGroupCreateStatic
            *     #define traceENTER_xEventGroupCreateStatic( pxEventGroupBuffer )
            * #endif
            * */
            traceENTER_xEventGroupCreateStatic( pxEventGroupBuffer );

            /** A StaticEventGroup_t object must be provided. 
             * 断言：当传入值为 0 时，触发一个中断，源码如下:
             *  #define configASSERT( x )         \
                    if( ( x ) == 0 )              \
                    {                             \
                        taskDISABLE_INTERRUPTS(); \
                        for( ; ; )                \
                        ;                         \
                    }
            * 定义位置： FreeRTOSConfig.h
            * 注意：taskDISABLE_INTERRUPTS() 的具体实现需根据不同平台自行完成
            */
            configASSERT( pxEventGroupBuffer );

            /**
             * configASSERT_DEFINED 用于观察当前代码是否实现 configASSERT （非 0 断言判断）
             * 源码如下:
             *  #ifndef configASSERT
                    #define configASSERT( x )
                    #define configASSERT_DEFINED    0
                #else
                    #define configASSERT_DEFINED    1
                #endif
            * */
            #if ( configASSERT_DEFINED == 1 )
            {
                /** Sanity check that the size of the structure used to declare a variable of type StaticEventGroup_t equals the size of the real event group structure. 
                 * 健全检查：若 StaticEventGroup_t 和 EventGroup_t 的大小不一致，就可能会导致内存访问越界或者数据损坏的问题，因此在此处强制检查，
                 * 确保静态分配的内存块大小和实际事件组所需的内存大小一致，从而保证内存的兼容性
                 **/
                volatile size_t xSize = sizeof( StaticEventGroup_t );
                configASSERT( xSize == sizeof( EventGroup_t ) );
            }
            // 良好的编码规范：#endif 后放置对应的 #if 内容，方便查看
            #endif /* configASSERT_DEFINED */

            /* The user has provided a statically allocated event group - use it. */
            /* MISRA Ref 11.3.1 [Misaligned access] */
            /** More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-113 
             * 该要求(MISRA.md#rule-113)原文如下：A cast shall not be performed between a pointer to object type and a pointer to a different object type
             * 即：不得在指向不同对象类型的指针之间进行强制类型转换。
             * 完整条文翻译：该规则禁止将对象类型指针强制转换为不同对象类型的指针，因为这可能导致指针未正确对齐，进而引发未定义行为。
             * 即使强制转换生成了一个正确对齐的指针，若使用该指针访问对象，其行为仍可能是未定义的。
             * 出于数据隐藏目的，FreeRTOS 特意为所有内核对象类型（StaticEventGroup_t、StaticQueue_t、StaticStreamBuffer_t、StaticTimer_t 和 StaticTask_t）创建了外部别名。
             * 内部对象类型与对应的外部别名保证具有相同的大小和对齐方式，这一点通过 configASSERT 进行检查。
            */
            /** coverity[misra_c_2012_rule_11_3_violation] 
             * Coverity 是一款静态代码分析工具，用于检测代码中的潜在缺陷和安全漏洞。这个注释是告诉 Coverity 工具，当前代码行可能违反了 MISRA C 2012 标准中的规则 11.3
            */
            // 对开发者要求： 开发者需自行保证此处类型转换不会造成内存相关问题，即传入指针对应结构与强制转换后的新结构占用内存空间应一致
            pxEventBits = ( EventGroup_t * ) pxEventGroupBuffer;

            if( pxEventBits != NULL )
            {
                pxEventBits->uxEventBits = 0; //事件标志位清零
                vListInitialise( &( pxEventBits->xTasksWaitingForBits ) );

                #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
                {
                    /* Both static and dynamic allocation can be used, so note that
                     * this event group was created statically in case the event group
                     * is later deleted. */
                    //通过标志位声明该事件组是静态分配
                    pxEventBits->ucStaticallyAllocated = pdTRUE;
                }
                #endif /* configSUPPORT_DYNAMIC_ALLOCATION */

                //日志记录：事件组创建成功
                traceEVENT_GROUP_CREATE( pxEventBits );
            }
            else
            {
                /* xEventGroupCreateStatic should only ever be called with
                 * pxEventGroupBuffer pointing to a pre-allocated (compile time
                 * allocated) StaticEventGroup_t variable. */
                //日志记录：事件组创建失败
                traceEVENT_GROUP_CREATE_FAILED();
            }

            traceRETURN_xEventGroupCreateStatic( pxEventBits );

            return pxEventBits;
        }

    #endif /* configSUPPORT_STATIC_ALLOCATION */
/*-----------------------------------------------------------*/

    /**
     *  参照上方函数注释中对于创建事件组时的静态（static）与动态（dynamic）的分别的说明，此时无需入参，但是需要通过 pvPortMalloc 自行分配一个内存
     *  需保证：通过 pvPortMalloc 分配的内存应能实现对齐，与系统架构相关；其余操作与静态事件组创建相似
     * */
    #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )

        EventGroupHandle_t xEventGroupCreate( void )
        {
            EventGroup_t * pxEventBits;

            traceENTER_xEventGroupCreate();

            /* MISRA Ref 11.5.1 [Malloc memory assignment] */
            /** More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 
             * MISRA C:2012 11.5：不应将空指针（指向 void 类型的指针）转换为对象指针，因为可能会导致指针未正确对齐，从而引发未定义行为。
             * 11.5.1：pvPortMalloc() 函数返回的内存块保证能满足由 portBYTE_ALIGNMENT 指定的架构对齐要求。因此，对 pvPortMalloc() 返回的空指针进行类型转换是安全的，因为该指针保证是对齐的。
            */
            /* coverity[misra_c_2012_rule_11_5_violation] */
            //Coverity 是一款静态代码分析工具，用于检测代码中的潜在缺陷和安全漏洞。上方注释是告诉 Coverity 工具，当前代码行可能违反了 MISRA C 2012 标准中的规则 11.5
            //pvPortMalloc 中的 v 为 void，查看函数定义，该函数返回一个 void* 类型指针，该函数在不同架构下均有实现，如：IAR/ARM_CM23，GCC/ARM_CM23 等
            pxEventBits = ( EventGroup_t * ) pvPortMalloc( sizeof( EventGroup_t ) );

            if( pxEventBits != NULL )
            {
                pxEventBits->uxEventBits = 0;
                vListInitialise( &( pxEventBits->xTasksWaitingForBits ) );

                #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
                {
                    /* Both static and dynamic allocation can be used, so note this
                     * event group was allocated statically in case the event group is
                     * later deleted. */
                    pxEventBits->ucStaticallyAllocated = pdFALSE;
                }
                #endif /* configSUPPORT_STATIC_ALLOCATION */

                traceEVENT_GROUP_CREATE( pxEventBits );
            }
            else
            {
                traceEVENT_GROUP_CREATE_FAILED();
            }

            traceRETURN_xEventGroupCreate( pxEventBits );

            return pxEventBits;
        }

    #endif /* configSUPPORT_DYNAMIC_ALLOCATION */
/*-----------------------------------------------------------*/

    /**
     * xTicksToWait ：指定任务的最大等待时间，以系统时钟节拍为单位
     * xEventGroupSync ：主要用于实现多个任务间的会合（ rendezvous ）。
     * 会合（ rendezvous ）：多个任务在某个特定的同步点等待，直到所有参与会合的任务都到达该点，然后这些任务再继续执行后续操作。
     * 当前函数：让任务在设置事件组中的某些位的同时，等待事件组中的其他位被设置，从而实现任务间的同步。
     * 步骤1：将 uxBitsToSet 设置到事件组 xEventGroup 对应位置；
     * 步骤2：检查 uxBitsToWaitFor 指定的位是否都已经被设置；
     * 步骤3.1（分支）：若所有等待的位都已被设置，函数会清除这些位（会合操作通常会清除等待的位）
     * 步骤3.2（分支）（核心）：若有等待的位未被设置，任务会进入阻塞状态，等待这些位被设置或者超时。当等待的位被设置后，任务会被唤醒，清除这些位并返回事件组的当前值；若超时，任务也会被唤醒并返回事件组的当前值。
     * */
    EventBits_t xEventGroupSync( EventGroupHandle_t xEventGroup,
                                 const EventBits_t uxBitsToSet,
                                 const EventBits_t uxBitsToWaitFor,
                                 TickType_t xTicksToWait )
    {
        EventBits_t uxOriginalBitValue, uxReturn;
        EventGroup_t * pxEventBits = xEventGroup;
        BaseType_t xAlreadyYielded;
        BaseType_t xTimeoutOccurred = pdFALSE;

        traceENTER_xEventGroupSync( xEventGroup, uxBitsToSet, uxBitsToWaitFor, xTicksToWait );

        /** 变量类型上的统一：uxBitsToWaitFor 实际为 TickType_t 类型，与 eventEVENT_BITS_CONTROL_BYTES 在长度上应一致
         *  以下两个断言成立的条件为：uxBitsToWaitFor 的高8bit（系统内核区）均为0，其余bit不全为0
         *  设计原因为：每个 EventBits_t 类型变量的 事件组标识符（ 如 uxBitsToWaitFor ）中均会保留高 8 bit供系统内核使用，其余低位用来存储事件组
         * */
        configASSERT( ( uxBitsToWaitFor & eventEVENT_BITS_CONTROL_BYTES ) == 0 );
        configASSERT( uxBitsToWaitFor != 0 );

        /**
         * INCLUDE_xTaskGetSchedulerState ：配置宏，决定是否把 xTaskGetSchedulerState() 函数包含到 FreeRTOS 内核中
         * xTaskGetSchedulerState() 函数：获取当前调度器的状态，返回值包括：
         * taskSCHEDULER_NOT_STARTED：调度器未启动；
         * taskSCHEDULER_RUNNING：调度器正在运行；
         * taskSCHEDULER_SUSPENDED：调度器已暂停；
         * configUSE_TIMERS ： 配置宏，决定 FreeRTOS 内核是否启用软件定时器，是否能够启用内核的定时/延迟相关的API
         * 以下断言要求调度器不能处于暂停状态，且事件组延迟 xTicksToWait 不能为 0
         * */
        #if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
        {
            configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
        }
        #endif

        /** 
         * 暂停所有任务的调度，使得下方关键任务能够持续执行，而不会被其他任务抢占，直到后续通过调用 xTaskResumeAll() 函数恢复调度；
         * 因此下方通过 { } 包含了不能被打断执行的关键代码，是良好的编码风格，格式为：
         * vTaskSuspendAll();
         * { 关键代码 }
         * xTaskResumeAll()；
         * */
        vTaskSuspendAll();
        {
            uxOriginalBitValue = pxEventBits->uxEventBits;

            ( void ) xEventGroupSetBits( xEventGroup, uxBitsToSet );

            /**
             * uxOriginalBitValue：事件组 xEventGroup 在调用 xEventGroupSetBits 函数之前的原始事件组位
             * uxBitsToSet：当前要设置的事件组位
             * uxBitsToWaitFor：等待事件组位（可大致理解为一个用于判断是否“会和（ rendezvous ）”的“目标值”），“指定了当前任务需要等待被设置的事件组位。只有当这些位都被设置后，任务才会继续执行后续操作”，此处的“事件”可以当作“资源”的抽象表示，即“任务能够继续运行的条件”；
             * 函数 xEventGroupSetBits 中会执行 pxEventBits->uxEventBits |= uxBitsToSet 将 xEventGroup 中的事件组位进行更新，
             * 因此下方 if 中 ( uxOriginalBitValue | uxBitsToSet )实际上就是更新后的事件组位
             * 下方 if 语句实际上是判断更新后的 事件组位 与目标 uxBitsToWaitFor 是否完全相同，如果相同，即达到“会和”状态（任务所需的事件均已满足，可继续执行）
             * “rendezvous”（会合）在 freertos 中用于任务间同步，旨在确保多个任务在特定条件满足时才会继续执行后续操作；
             * “rendezvous” 意味着多个任务需要在某个 “会合点” 相遇，只有当所有参与会合的任务都到达这个会合点（即满足特定的同步条件）时，这些任务才会继续执行后续的代码逻辑。这种机制常用于需要多个任务协作完成特定任务的场景，确保任务之间的操作顺序和数据一致性。
             * */
            if( ( ( uxOriginalBitValue | uxBitsToSet ) & uxBitsToWaitFor ) == uxBitsToWaitFor )
            {
                /* All the rendezvous bits are now set - no need to block. */
                uxReturn = ( uxOriginalBitValue | uxBitsToSet );

                /** Rendezvous always clear the bits.  They will have been cleared
                 * already unless this is the only task in the rendezvous. 
                 * “会和”判断后通常会清除其他不必要的标志位，只保留 uxBitsToWaitFor 
                 * */
                pxEventBits->uxEventBits &= ~uxBitsToWaitFor;

                //xTicksToWait 清零，即后续无需等待，可以继续运行，当前函数后续没有其他逻辑相关工作
                xTicksToWait = 0;
            }
            else
            {
                //若未达到会和条件
                if( xTicksToWait != ( TickType_t ) 0 )
                {
                    traceEVENT_GROUP_SYNC_BLOCK( xEventGroup, uxBitsToSet, uxBitsToWaitFor );

                    /* Store the bits that the calling task is waiting for in the
                     * task's event list item so the kernel knows when a match is
                     * found.  Then enter the blocked state. 
                     * 翻译：将调用任务正在等待的位存储到该任务的事件列表项中，以便内核知晓何时找到匹配条件，随后进入阻塞状态。
                     * vTaskPlaceOnUnorderedEventList ： 将任务放置到一个无序事件列表（即：pxEventBits->xTasksWaitingForBits ），让任务进入等待状态，直至特定事件发生
                     * eventCLEAR_EVENTS_ON_EXIT_BIT ： 当任务等待的位满足条件并退出等待状态时，会自动清除事件组中相应的位
                     * eventWAIT_FOR_ALL_BITS ： 任务必须等待 uxBitsToWaitFor 中指定的所有位都被设置，才会从等待状态中解除阻塞
                     * 这里的“等待状态”是指调用当前函数的任务的状态
                     * xTicksToWait ：指定任务的最大等待时间，以系统时钟节拍为单位，若设为 portMAX_DELAY，则任务会无限期等待，直到等待的位满足条件，若设为具体值，则任务最多等待这么多个时钟节拍，超时后会自动解除阻塞
                     * 实际作用：让当前任务进入等待状态（阻塞），等待事件组中指定的位被设置，最多等待 xTicksToWait 个系统节拍时间
                     * */
                    vTaskPlaceOnUnorderedEventList( &( pxEventBits->xTasksWaitingForBits ), ( uxBitsToWaitFor | eventCLEAR_EVENTS_ON_EXIT_BIT | eventWAIT_FOR_ALL_BITS ), xTicksToWait );

                    /* This assignment is obsolete as uxReturn will get set after
                     * the task unblocks, but some compilers mistakenly generate a
                     * warning about uxReturn being returned without being set if the
                     * assignment is omitted. 
                     * 翻译：下面对 uxReturn 的赋值操作其实已无必要，因为在任务解除阻塞（调用 xTaskResumeAll() ）后 uxReturn 会再被赋值。
                     * 但若省略下方赋值，一些编译器会错误地发出警告，提示 uxReturn 在未赋值的情况下被返回
                     * */
                    uxReturn = 0;
                }
                else
                {
                    /* The rendezvous bits were not set, but no block time was
                     * specified - just return the current event bit value. 
                     * 可能的错误情况：既不满足会和条件，又未指定延迟时间，则报错
                     */
                    uxReturn = pxEventBits->uxEventBits;
                    xTimeoutOccurred = pdTRUE;
                }
            }
        }
        /** vTaskSuspendAll 会增加调度器挂起计数，xTaskResumeAll 会减少计数，当计数回到 0 时，调度器恢复活动状态。
         *  当挂起计数为 0 时，xTaskResumeAll 会返回 pdFALSE ，此时需立即通过 taskYIELD_WITHIN_API() 强制触发一次调度，因为此时可能已经有更高等级的任务在等待运行
         * */
        xAlreadyYielded = xTaskResumeAll();

        if( xTicksToWait != ( TickType_t ) 0 )
        {
            if( xAlreadyYielded == pdFALSE )
            {
                // taskYIELD_WITHIN_API() ：强制进行一次任务上下文切换，调用后 FreeRTOS 调度器会暂停当前正在执行的任务，并根据任务的优先级和调度算法选择下一个要执行的任务
                // 调用后，当前函数后续逻辑仍然会执行，不过是在等调度器再次切换到当前任务时；
                taskYIELD_WITHIN_API();
            }
            else
            {
                //mtCOVERAGE_TEST_MARKER ： 用于代码覆盖率测试的标记宏，可无需实际实现
                mtCOVERAGE_TEST_MARKER();
            }

            /* The task blocked to wait for its required bits to be set - at this
             * point either the required bits were set or the block time expired.  If
             * the required bits were set they will have been stored in the task's
             * event list item, and they should now be retrieved then cleared. 
             * 翻译：任务进入阻塞状态以等待其所需的位被设置 —— 此时，要么所需的位已被设置，要么阻塞时间已到期。如果所需的位已被设置，这些位会被存储在任务的事件列表项中，现在需找出这些位并清除
             * 获取当前任务的事件列表项在重置之前的值（ xItemValue ），这个值包含了任务解除阻塞的原因信息，特别是是否因为等待的事件位被设置而解除阻塞的标志，随后进行重置
             * */
            uxReturn = uxTaskResetEventItemValue();

            /**
             * 用于判断任务是因为等待的事件位被设置而解除阻塞，还是因为等待超时才解除阻塞
             * eventUNBLOCKED_DUE_TO_BIT_SET ： 表示任务是因为等待的事件位被设置而解除阻塞
             * 若下方 if 成立，表示是因为超时才解除的阻塞
             * */
            if( ( uxReturn & eventUNBLOCKED_DUE_TO_BIT_SET ) == ( EventBits_t ) 0 )
            {
                /* The task timed out, just return the current event bit value. */
                // 进入临界区，防止在读取和修改事件组的位时被其他任务或中断打断
                taskENTER_CRITICAL();
                {
                    uxReturn = pxEventBits->uxEventBits;

                    /** Although the task got here because it timed out before the
                     * bits it was waiting for were set, it is possible that since it
                     * unblocked another task has set the bits.  If this is the case
                     * then it needs to clear the bits before exiting. 
                     * 翻译：尽管任务进入此状态是因为在其等待的位被设置之前就已超时，但在它解除阻塞之后，其他任务有可能已经设置了这些位。这种情况下，任务在退出前需清除这些位
                     * 在等待超时后，检查 uxBitsToWaitFor 对应位是否已经被其他任务设置，如果已设置，表示这些位对应的事件已经被其他任务完成，需要将其清掉，避免后续出现无谓的重复工作
                     * */
                    if( ( uxReturn & uxBitsToWaitFor ) == uxBitsToWaitFor )
                    {
                        pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
                    }
                    else
                    {
                        mtCOVERAGE_TEST_MARKER();
                    }
                }
                // 退出临界区
                taskEXIT_CRITICAL();

                xTimeoutOccurred = pdTRUE;
            }
            else
            {
                /* The task unblocked because the bits were set. */
            }

            /* Control bits might be set as the task had blocked should not be
             * returned. */
            uxReturn &= ~eventEVENT_BITS_CONTROL_BYTES;
        }

        //跟踪信息中记录是否任务因超时解除注释
        traceEVENT_GROUP_SYNC_END( xEventGroup, uxBitsToSet, uxBitsToWaitFor, xTimeoutOccurred );

        /* Prevent compiler warnings when trace macros are not used. */
        // 下方语句只是为了消除某些编译器因为 xTimeoutOccurred 未使用而提出的 warning，无实际意义。当“会和”时，当前函数并不会操作 xTimeoutOccurred ，此时可能有编译器会提出 warning
        ( void ) xTimeoutOccurred;

        traceRETURN_xEventGroupSync( uxReturn );

        return uxReturn;
    }
/*-----------------------------------------------------------*/

    /**
     * 让任务等待事件组中的特定事件位被设置
     * */
    EventBits_t xEventGroupWaitBits( EventGroupHandle_t xEventGroup,
                                     const EventBits_t uxBitsToWaitFor,
                                     const BaseType_t xClearOnExit,
                                     const BaseType_t xWaitForAllBits,
                                     TickType_t xTicksToWait )
    {
        EventGroup_t * pxEventBits = xEventGroup;
        EventBits_t uxReturn, uxControlBits = 0;
        BaseType_t xWaitConditionMet, xAlreadyYielded;
        BaseType_t xTimeoutOccurred = pdFALSE;

        traceENTER_xEventGroupWaitBits( xEventGroup, uxBitsToWaitFor, xClearOnExit, xWaitForAllBits, xTicksToWait );

        /* Check the user is not attempting to wait on the bits used by the kernel
         * itself, and that at least one bit is being requested. */
        configASSERT( xEventGroup );
        configASSERT( ( uxBitsToWaitFor & eventEVENT_BITS_CONTROL_BYTES ) == 0 );
        configASSERT( uxBitsToWaitFor != 0 );
        #if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
        {
            configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
        }
        #endif

        vTaskSuspendAll();
        {
            const EventBits_t uxCurrentEventBits = pxEventBits->uxEventBits;

            /* Check to see if the wait condition is already met or not. */
            xWaitConditionMet = prvTestWaitCondition( uxCurrentEventBits, uxBitsToWaitFor, xWaitForAllBits );

            if( xWaitConditionMet != pdFALSE )
            {
                /* The wait condition has already been met so there is no need to
                 * block. */
                uxReturn = uxCurrentEventBits;
                xTicksToWait = ( TickType_t ) 0;

                /* Clear the wait bits if requested to do so. */
                if( xClearOnExit != pdFALSE )
                {
                    pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
                }
                else
                {
                    mtCOVERAGE_TEST_MARKER();
                }
            }
            else if( xTicksToWait == ( TickType_t ) 0 )
            {
                /* The wait condition has not been met, but no block time was
                 * specified, so just return the current value. */
                uxReturn = uxCurrentEventBits;
                xTimeoutOccurred = pdTRUE;
            }
            else
            {
                /* The task is going to block to wait for its required bits to be
                 * set.  uxControlBits are used to remember the specified behaviour of
                 * this call to xEventGroupWaitBits() - for use when the event bits
                 * unblock the task. */
                if( xClearOnExit != pdFALSE )
                {
                    uxControlBits |= eventCLEAR_EVENTS_ON_EXIT_BIT;
                }
                else
                {
                    mtCOVERAGE_TEST_MARKER();
                }

                if( xWaitForAllBits != pdFALSE )
                {
                    uxControlBits |= eventWAIT_FOR_ALL_BITS;
                }
                else
                {
                    mtCOVERAGE_TEST_MARKER();
                }

                /* Store the bits that the calling task is waiting for in the
                 * task's event list item so the kernel knows when a match is
                 * found.  Then enter the blocked state. */
                vTaskPlaceOnUnorderedEventList( &( pxEventBits->xTasksWaitingForBits ), ( uxBitsToWaitFor | uxControlBits ), xTicksToWait );

                /* This is obsolete as it will get set after the task unblocks, but
                 * some compilers mistakenly generate a warning about the variable
                 * being returned without being set if it is not done. */
                uxReturn = 0;

                traceEVENT_GROUP_WAIT_BITS_BLOCK( xEventGroup, uxBitsToWaitFor );
            }
        }
        xAlreadyYielded = xTaskResumeAll();

        if( xTicksToWait != ( TickType_t ) 0 )
        {
            if( xAlreadyYielded == pdFALSE )
            {
                taskYIELD_WITHIN_API();
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }

            /* The task blocked to wait for its required bits to be set - at this
             * point either the required bits were set or the block time expired.  If
             * the required bits were set they will have been stored in the task's
             * event list item, and they should now be retrieved then cleared. */
            uxReturn = uxTaskResetEventItemValue();

            if( ( uxReturn & eventUNBLOCKED_DUE_TO_BIT_SET ) == ( EventBits_t ) 0 )
            {
                taskENTER_CRITICAL();
                {
                    /* The task timed out, just return the current event bit value. */
                    uxReturn = pxEventBits->uxEventBits;

                    /* It is possible that the event bits were updated between this
                     * task leaving the Blocked state and running again. */
                    if( prvTestWaitCondition( uxReturn, uxBitsToWaitFor, xWaitForAllBits ) != pdFALSE )
                    {
                        if( xClearOnExit != pdFALSE )
                        {
                            pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
                        }
                        else
                        {
                            mtCOVERAGE_TEST_MARKER();
                        }
                    }
                    else
                    {
                        mtCOVERAGE_TEST_MARKER();
                    }

                    xTimeoutOccurred = pdTRUE;
                }
                taskEXIT_CRITICAL();
            }
            else
            {
                /* The task unblocked because the bits were set. */
            }

            /* The task blocked so control bits may have been set. */
            uxReturn &= ~eventEVENT_BITS_CONTROL_BYTES;
        }

        traceEVENT_GROUP_WAIT_BITS_END( xEventGroup, uxBitsToWaitFor, xTimeoutOccurred );

        /* Prevent compiler warnings when trace macros are not used. */
        ( void ) xTimeoutOccurred;

        traceRETURN_xEventGroupWaitBits( uxReturn );

        return uxReturn;
    }
/*-----------------------------------------------------------*/

    EventBits_t xEventGroupClearBits( EventGroupHandle_t xEventGroup,
                                      const EventBits_t uxBitsToClear )
    {
        EventGroup_t * pxEventBits = xEventGroup;
        EventBits_t uxReturn;

        traceENTER_xEventGroupClearBits( xEventGroup, uxBitsToClear );

        /* Check the user is not attempting to clear the bits used by the kernel
         * itself. */
        configASSERT( xEventGroup );
        configASSERT( ( uxBitsToClear & eventEVENT_BITS_CONTROL_BYTES ) == 0 );

        taskENTER_CRITICAL();
        {
            traceEVENT_GROUP_CLEAR_BITS( xEventGroup, uxBitsToClear );

            /* The value returned is the event group value prior to the bits being
             * cleared. */
            uxReturn = pxEventBits->uxEventBits;

            /* Clear the bits. */
            pxEventBits->uxEventBits &= ~uxBitsToClear;
        }
        taskEXIT_CRITICAL();

        traceRETURN_xEventGroupClearBits( uxReturn );

        return uxReturn;
    }
/*-----------------------------------------------------------*/

    #if ( ( configUSE_TRACE_FACILITY == 1 ) && ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 1 ) )

        BaseType_t xEventGroupClearBitsFromISR( EventGroupHandle_t xEventGroup,
                                                const EventBits_t uxBitsToClear )
        {
            BaseType_t xReturn;

            traceENTER_xEventGroupClearBitsFromISR( xEventGroup, uxBitsToClear );

            traceEVENT_GROUP_CLEAR_BITS_FROM_ISR( xEventGroup, uxBitsToClear );
            xReturn = xTimerPendFunctionCallFromISR( vEventGroupClearBitsCallback, ( void * ) xEventGroup, ( uint32_t ) uxBitsToClear, NULL );

            traceRETURN_xEventGroupClearBitsFromISR( xReturn );

            return xReturn;
        }

    #endif /* if ( ( configUSE_TRACE_FACILITY == 1 ) && ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 1 ) ) */
/*-----------------------------------------------------------*/

    EventBits_t xEventGroupGetBitsFromISR( EventGroupHandle_t xEventGroup )
    {
        UBaseType_t uxSavedInterruptStatus;
        EventGroup_t const * const pxEventBits = xEventGroup;
        EventBits_t uxReturn;

        traceENTER_xEventGroupGetBitsFromISR( xEventGroup );

        /* MISRA Ref 4.7.1 [Return value shall be checked] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#dir-47 */
        /* coverity[misra_c_2012_directive_4_7_violation] */
        uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
        {
            uxReturn = pxEventBits->uxEventBits;
        }
        taskEXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );

        traceRETURN_xEventGroupGetBitsFromISR( uxReturn );

        return uxReturn;
    }
/*-----------------------------------------------------------*/

    EventBits_t xEventGroupSetBits( EventGroupHandle_t xEventGroup,
                                    const EventBits_t uxBitsToSet )
    {
        ListItem_t * pxListItem;
        ListItem_t * pxNext;
        ListItem_t const * pxListEnd;
        List_t const * pxList;
        EventBits_t uxBitsToClear = 0, uxBitsWaitedFor, uxControlBits, uxReturnBits;
        EventGroup_t * pxEventBits = xEventGroup;
        BaseType_t xMatchFound = pdFALSE;

        traceENTER_xEventGroupSetBits( xEventGroup, uxBitsToSet );

        /* Check the user is not attempting to set the bits used by the kernel
         * itself. */
        configASSERT( xEventGroup );
        configASSERT( ( uxBitsToSet & eventEVENT_BITS_CONTROL_BYTES ) == 0 );

        pxList = &( pxEventBits->xTasksWaitingForBits );
        pxListEnd = listGET_END_MARKER( pxList );
        vTaskSuspendAll();
        {
            traceEVENT_GROUP_SET_BITS( xEventGroup, uxBitsToSet );

            pxListItem = listGET_HEAD_ENTRY( pxList );

            /* Set the bits. */
            pxEventBits->uxEventBits |= uxBitsToSet;

            /* See if the new bit value should unblock any tasks. */
            while( pxListItem != pxListEnd )
            {
                pxNext = listGET_NEXT( pxListItem );
                uxBitsWaitedFor = listGET_LIST_ITEM_VALUE( pxListItem );
                xMatchFound = pdFALSE;

                /* Split the bits waited for from the control bits. */
                uxControlBits = uxBitsWaitedFor & eventEVENT_BITS_CONTROL_BYTES;
                uxBitsWaitedFor &= ~eventEVENT_BITS_CONTROL_BYTES;

                if( ( uxControlBits & eventWAIT_FOR_ALL_BITS ) == ( EventBits_t ) 0 )
                {
                    /* Just looking for single bit being set. */
                    if( ( uxBitsWaitedFor & pxEventBits->uxEventBits ) != ( EventBits_t ) 0 )
                    {
                        xMatchFound = pdTRUE;
                    }
                    else
                    {
                        mtCOVERAGE_TEST_MARKER();
                    }
                }
                else if( ( uxBitsWaitedFor & pxEventBits->uxEventBits ) == uxBitsWaitedFor )
                {
                    /* All bits are set. */
                    xMatchFound = pdTRUE;
                }
                else
                {
                    /* Need all bits to be set, but not all the bits were set. */
                }

                if( xMatchFound != pdFALSE )
                {
                    /* The bits match.  Should the bits be cleared on exit? */
                    if( ( uxControlBits & eventCLEAR_EVENTS_ON_EXIT_BIT ) != ( EventBits_t ) 0 )
                    {
                        uxBitsToClear |= uxBitsWaitedFor;
                    }
                    else
                    {
                        mtCOVERAGE_TEST_MARKER();
                    }

                    /* Store the actual event flag value in the task's event list
                     * item before removing the task from the event list.  The
                     * eventUNBLOCKED_DUE_TO_BIT_SET bit is set so the task knows
                     * that is was unblocked due to its required bits matching, rather
                     * than because it timed out. */
                    vTaskRemoveFromUnorderedEventList( pxListItem, pxEventBits->uxEventBits | eventUNBLOCKED_DUE_TO_BIT_SET );
                }

                /* Move onto the next list item.  Note pxListItem->pxNext is not
                 * used here as the list item may have been removed from the event list
                 * and inserted into the ready/pending reading list. */
                pxListItem = pxNext;
            }

            /* Clear any bits that matched when the eventCLEAR_EVENTS_ON_EXIT_BIT
             * bit was set in the control word. */
            pxEventBits->uxEventBits &= ~uxBitsToClear;

            /* Snapshot resulting bits. */
            uxReturnBits = pxEventBits->uxEventBits;
        }
        ( void ) xTaskResumeAll();

        traceRETURN_xEventGroupSetBits( uxReturnBits );

        return uxReturnBits;
    }
/*-----------------------------------------------------------*/

    void vEventGroupDelete( EventGroupHandle_t xEventGroup )
    {
        EventGroup_t * pxEventBits = xEventGroup;
        const List_t * pxTasksWaitingForBits;

        traceENTER_vEventGroupDelete( xEventGroup );

        configASSERT( pxEventBits );

        pxTasksWaitingForBits = &( pxEventBits->xTasksWaitingForBits );

        vTaskSuspendAll();
        {
            traceEVENT_GROUP_DELETE( xEventGroup );

            while( listCURRENT_LIST_LENGTH( pxTasksWaitingForBits ) > ( UBaseType_t ) 0 )
            {
                /* Unblock the task, returning 0 as the event list is being deleted
                 * and cannot therefore have any bits set. */
                configASSERT( pxTasksWaitingForBits->xListEnd.pxNext != ( const ListItem_t * ) &( pxTasksWaitingForBits->xListEnd ) );
                vTaskRemoveFromUnorderedEventList( pxTasksWaitingForBits->xListEnd.pxNext, eventUNBLOCKED_DUE_TO_BIT_SET );
            }
        }
        ( void ) xTaskResumeAll();

        #if ( ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) && ( configSUPPORT_STATIC_ALLOCATION == 0 ) )
        {
            /* The event group can only have been allocated dynamically - free
             * it again. */
            vPortFree( pxEventBits );
        }
        #elif ( ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) && ( configSUPPORT_STATIC_ALLOCATION == 1 ) )
        {
            /* The event group could have been allocated statically or
             * dynamically, so check before attempting to free the memory. */
            if( pxEventBits->ucStaticallyAllocated == ( uint8_t ) pdFALSE )
            {
                vPortFree( pxEventBits );
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }
        }
        #endif /* configSUPPORT_DYNAMIC_ALLOCATION */

        traceRETURN_vEventGroupDelete();
    }
/*-----------------------------------------------------------*/

    #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
        BaseType_t xEventGroupGetStaticBuffer( EventGroupHandle_t xEventGroup,
                                               StaticEventGroup_t ** ppxEventGroupBuffer )
        {
            BaseType_t xReturn;
            EventGroup_t * pxEventBits = xEventGroup;

            traceENTER_xEventGroupGetStaticBuffer( xEventGroup, ppxEventGroupBuffer );

            configASSERT( pxEventBits );
            configASSERT( ppxEventGroupBuffer );

            #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
            {
                /* Check if the event group was statically allocated. */
                if( pxEventBits->ucStaticallyAllocated == ( uint8_t ) pdTRUE )
                {
                    /* MISRA Ref 11.3.1 [Misaligned access] */
                    /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-113 */
                    /* coverity[misra_c_2012_rule_11_3_violation] */
                    *ppxEventGroupBuffer = ( StaticEventGroup_t * ) pxEventBits;
                    xReturn = pdTRUE;
                }
                else
                {
                    xReturn = pdFALSE;
                }
            }
            #else /* configSUPPORT_DYNAMIC_ALLOCATION */
            {
                /* Event group must have been statically allocated. */
                /* MISRA Ref 11.3.1 [Misaligned access] */
                /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-113 */
                /* coverity[misra_c_2012_rule_11_3_violation] */
                *ppxEventGroupBuffer = ( StaticEventGroup_t * ) pxEventBits;
                xReturn = pdTRUE;
            }
            #endif /* configSUPPORT_DYNAMIC_ALLOCATION */

            traceRETURN_xEventGroupGetStaticBuffer( xReturn );

            return xReturn;
        }
    #endif /* configSUPPORT_STATIC_ALLOCATION */
/*-----------------------------------------------------------*/

/* For internal use only - execute a 'set bits' command that was pended from
 * an interrupt. 
 * 以下两个回调（ vEventGroupSetBitsCallback 和 vEventGroupClearBitsCallback）仅能供内核调用
 */
    void vEventGroupSetBitsCallback( void * pvEventGroup,
                                     uint32_t ulBitsToSet )
    {
        traceENTER_vEventGroupSetBitsCallback( pvEventGroup, ulBitsToSet );

        /* MISRA Ref 11.5.4 [Callback function parameter] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
        /* coverity[misra_c_2012_rule_11_5_violation] */
        ( void ) xEventGroupSetBits( pvEventGroup, ( EventBits_t ) ulBitsToSet );

        traceRETURN_vEventGroupSetBitsCallback();
    }
/*-----------------------------------------------------------*/

/* For internal use only - execute a 'clear bits' command that was pended from
 * an interrupt. */
    void vEventGroupClearBitsCallback( void * pvEventGroup,
                                       uint32_t ulBitsToClear )
    {
        traceENTER_vEventGroupClearBitsCallback( pvEventGroup, ulBitsToClear );

        /* MISRA Ref 11.5.4 [Callback function parameter] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
        /* coverity[misra_c_2012_rule_11_5_violation] */
        ( void ) xEventGroupClearBits( pvEventGroup, ( EventBits_t ) ulBitsToClear );

        traceRETURN_vEventGroupClearBitsCallback();
    }
/*-----------------------------------------------------------*/

    /**
     * prv:private，对应 static 修饰符
     * 若 xWaitForAllBits 为真，返回 uxCurrentEventBits 和 uxBitsToWaitFor 的 bit 1 是否完全一致
     * 若 xWaitForAllBits 为假，判断 uxCurrentEventBits 和 uxBitsToWaitFor 是否有重合的 bit 1
     * */
    static BaseType_t prvTestWaitCondition( const EventBits_t uxCurrentEventBits,
                                            const EventBits_t uxBitsToWaitFor,
                                            const BaseType_t xWaitForAllBits )
    {
        BaseType_t xWaitConditionMet = pdFALSE;

        if( xWaitForAllBits == pdFALSE )
        {
            /* Task only has to wait for one bit within uxBitsToWaitFor to be
             * set.  Is one already set? */
            if( ( uxCurrentEventBits & uxBitsToWaitFor ) != ( EventBits_t ) 0 )
            {
                xWaitConditionMet = pdTRUE;
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }
        }
        else
        {
            /* Task has to wait for all the bits in uxBitsToWaitFor to be set.
             * Are they set already? */
            if( ( uxCurrentEventBits & uxBitsToWaitFor ) == uxBitsToWaitFor )
            {
                xWaitConditionMet = pdTRUE;
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }
        }

        return xWaitConditionMet;
    }
/*-----------------------------------------------------------*/

    /**
     * INCLUDE_xTimerPendFunctionCall：控制是否启用延迟函数调用（Pend Function Call）
     * 核心是调用 xTimerPendFunctionCallFromISR 函数实现时间延迟，以及调用回调 vEventGroupSetBitsCallback 实现设置bit位
     * 在核心两个函数的基础上做了一层简单封装
     * */
    #if ( ( configUSE_TRACE_FACILITY == 1 ) && ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 1 ) )

        BaseType_t xEventGroupSetBitsFromISR( EventGroupHandle_t xEventGroup,
                                              const EventBits_t uxBitsToSet,
                                              BaseType_t * pxHigherPriorityTaskWoken )
        {
            BaseType_t xReturn;

            traceENTER_xEventGroupSetBitsFromISR( xEventGroup, uxBitsToSet, pxHigherPriorityTaskWoken );

            traceEVENT_GROUP_SET_BITS_FROM_ISR( xEventGroup, uxBitsToSet );
            xReturn = xTimerPendFunctionCallFromISR( vEventGroupSetBitsCallback, ( void * ) xEventGroup, ( uint32_t ) uxBitsToSet, pxHigherPriorityTaskWoken );

            traceRETURN_xEventGroupSetBitsFromISR( xReturn );

            return xReturn;
        }

    #endif /* if ( ( configUSE_TRACE_FACILITY == 1 ) && ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 1 ) ) */
/*-----------------------------------------------------------*/

    #if ( configUSE_TRACE_FACILITY == 1 )

        UBaseType_t uxEventGroupGetNumber( void * xEventGroup )
        {
            UBaseType_t xReturn;

            /* MISRA Ref 11.5.2 [Opaque pointer] */
            /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
            /* coverity[misra_c_2012_rule_11_5_violation] */
            EventGroup_t const * pxEventBits = ( EventGroup_t * ) xEventGroup;

            traceENTER_uxEventGroupGetNumber( xEventGroup );

            if( xEventGroup == NULL )
            {
                xReturn = 0;
            }
            else
            {
                xReturn = pxEventBits->uxEventGroupNumber;
            }

            traceRETURN_uxEventGroupGetNumber( xReturn );

            return xReturn;
        }

    #endif /* configUSE_TRACE_FACILITY */
/*-----------------------------------------------------------*/

    #if ( configUSE_TRACE_FACILITY == 1 )

        //设置事件组编号，正式发布版本无需调试跟踪，因此将编号相关信息也放在调试范围内
        void vEventGroupSetNumber( void * xEventGroup,
                                   UBaseType_t uxEventGroupNumber )
        {
            traceENTER_vEventGroupSetNumber( xEventGroup, uxEventGroupNumber );

            /* MISRA Ref 11.5.2 [Opaque pointer] */
            /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
            /* coverity[misra_c_2012_rule_11_5_violation] */
            ( ( EventGroup_t * ) xEventGroup )->uxEventGroupNumber = uxEventGroupNumber;

            traceRETURN_vEventGroupSetNumber();
        }

    #endif /* configUSE_TRACE_FACILITY */
/*-----------------------------------------------------------*/

/* This entire source file will be skipped if the application is not configured
 * to include event groups functionality. If you want to include event groups
 * then ensure configUSE_EVENT_GROUPS is set to 1 in FreeRTOSConfig.h. */
#endif /* configUSE_EVENT_GROUPS == 1 */
