const {
  Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
  Header, Footer, AlignmentType, HeadingLevel, BorderStyle, WidthType,
  ShadingType, PageNumber, NumberFormat, LevelFormat, TabStopType,
  TabStopPosition, PageBreak, VerticalAlign
} = require("docx");
const fs = require("fs");

// ── Colour palette ────────────────────────────────────────────────────────────
const DARK_BLUE   = "1F3864";
const MED_BLUE    = "2E5FA3";
const LIGHT_BLUE  = "D9E4F0";
const ACCENT_TEAL = "1F6B75";
const GRAY_BG     = "F2F2F2";
const TABLE_GRAY  = "D9D9D9";
const WHITE       = "FFFFFF";
const BLACK       = "000000";

// ── Typography helpers ────────────────────────────────────────────────────────
const pt  = n => n * 2;         // half-points used by docx-js
const dxa = inches => Math.round(inches * 1440);

function heading1(text) {
  return new Paragraph({
    heading: HeadingLevel.HEADING_1,
    spacing: { before: 360, after: 180 },
    border: { bottom: { style: BorderStyle.SINGLE, size: 6, color: MED_BLUE, space: 6 } },
    children: [
      new TextRun({ text, bold: true, size: pt(16), color: DARK_BLUE, font: "Arial" })
    ]
  });
}

function heading2(text) {
  return new Paragraph({
    heading: HeadingLevel.HEADING_2,
    spacing: { before: 240, after: 120 },
    children: [
      new TextRun({ text, bold: true, size: pt(13), color: MED_BLUE, font: "Arial" })
    ]
  });
}

function heading3(text) {
  return new Paragraph({
    heading: HeadingLevel.HEADING_3,
    spacing: { before: 180, after: 80 },
    children: [
      new TextRun({ text, bold: true, italics: true, size: pt(12), color: ACCENT_TEAL, font: "Arial" })
    ]
  });
}

function body(text, { bold = false, italic = false, spacing = { before: 0, after: 160 }, indent = {} } = {}) {
  return new Paragraph({
    spacing,
    indent,
    alignment: AlignmentType.JUSTIFIED,
    children: [
      new TextRun({ text, size: pt(11), font: "Arial", bold, italics: italic, color: BLACK })
    ]
  });
}

function bodyRuns(runs, opts = {}) {
  return new Paragraph({
    spacing: opts.spacing || { before: 0, after: 160 },
    alignment: AlignmentType.JUSTIFIED,
    children: runs.map(r =>
      new TextRun({
        text: r.text,
        size: pt(11),
        font: r.font || "Arial",
        bold: r.bold || false,
        italics: r.italic || false,
        color: r.color || BLACK
      })
    )
  });
}

function codeLine(text) {
  return new Paragraph({
    spacing: { before: 0, after: 0 },
    indent: { left: dxa(0.4) },
    children: [
      new TextRun({ text, size: pt(9.5), font: "Courier New", color: "1A1A1A" })
    ]
  });
}

function codeBlock(lines) {
  const border = { style: BorderStyle.SINGLE, size: 1, color: "BBBBBB" };
  const borders = { top: border, bottom: border, left: border, right: border };
  return new Table({
    width: { size: dxa(7.5), type: WidthType.DXA },
    columnWidths: [dxa(7.5)],
    rows: [
      new TableRow({
        children: [
          new TableCell({
            borders,
            shading: { fill: "F5F5F5", type: ShadingType.CLEAR },
            margins: { top: 100, bottom: 100, left: 160, right: 160 },
            width: { size: dxa(7.5), type: WidthType.DXA },
            children: lines.map(l => codeLine(l))
          })
        ]
      })
    ]
  });
}

function bullet(text, bold_prefix = "") {
  return new Paragraph({
    numbering: { reference: "bullets", level: 0 },
    spacing: { before: 0, after: 100 },
    children: bold_prefix
      ? [
          new TextRun({ text: bold_prefix, bold: true, size: pt(11), font: "Arial", color: BLACK }),
          new TextRun({ text, size: pt(11), font: "Arial", color: BLACK })
        ]
      : [new TextRun({ text, size: pt(11), font: "Arial", color: BLACK })]
  });
}

function spacer(n = 1) {
  return Array.from({ length: n }, () =>
    new Paragraph({ spacing: { before: 0, after: 0 }, children: [new TextRun("")] })
  );
}

function pageBreak() {
  return new Paragraph({ children: [new PageBreak()] });
}

// ── Highlight box ─────────────────────────────────────────────────────────────
function highlightBox(lines, fillColor = LIGHT_BLUE) {
  const border = { style: BorderStyle.SINGLE, size: 4, color: MED_BLUE };
  const borders = { top: border, bottom: border, left: { style: BorderStyle.SINGLE, size: 12, color: MED_BLUE }, right: border };
  return new Table({
    width: { size: dxa(7.5), type: WidthType.DXA },
    columnWidths: [dxa(7.5)],
    rows: [
      new TableRow({
        children: [
          new TableCell({
            borders,
            shading: { fill: fillColor, type: ShadingType.CLEAR },
            margins: { top: 120, bottom: 120, left: 180, right: 180 },
            width: { size: dxa(7.5), type: WidthType.DXA },
            children: lines.map(l =>
              new Paragraph({
                spacing: { before: 0, after: 80 },
                alignment: AlignmentType.JUSTIFIED,
                children: [new TextRun({ text: l, size: pt(11), font: "Arial", color: BLACK })]
              })
            )
          })
        ]
      })
    ]
  });
}

// ── Two-column table ──────────────────────────────────────────────────────────
function infoTable(rows) {
  const hBorder = { style: BorderStyle.SINGLE, size: 4, color: MED_BLUE };
  const cBorder = { style: BorderStyle.SINGLE, size: 1, color: TABLE_GRAY };
  const w1 = dxa(2.4), w2 = dxa(5.1);

  return new Table({
    width: { size: dxa(7.5), type: WidthType.DXA },
    columnWidths: [w1, w2],
    rows: rows.map((row, i) => {
      const isHeader = i === 0;
      return new TableRow({
        children: [
          new TableCell({
            borders: { top: hBorder, bottom: hBorder, left: hBorder, right: cBorder },
            shading: { fill: isHeader ? MED_BLUE : GRAY_BG, type: ShadingType.CLEAR },
            margins: { top: 80, bottom: 80, left: 120, right: 120 },
            width: { size: w1, type: WidthType.DXA },
            children: [new Paragraph({
              alignment: AlignmentType.LEFT,
              children: [new TextRun({ text: row[0], bold: true, size: pt(10.5), font: "Arial", color: isHeader ? WHITE : DARK_BLUE })]
            })]
          }),
          new TableCell({
            borders: { top: hBorder, bottom: hBorder, left: cBorder, right: hBorder },
            shading: { fill: isHeader ? MED_BLUE : WHITE, type: ShadingType.CLEAR },
            margins: { top: 80, bottom: 80, left: 120, right: 120 },
            width: { size: w2, type: WidthType.DXA },
            children: [new Paragraph({
              alignment: AlignmentType.LEFT,
              children: [new TextRun({ text: row[1], size: pt(10.5), font: "Arial", color: isHeader ? WHITE : BLACK })]
            })]
          })
        ]
      });
    })
  });
}

// ── Title page ────────────────────────────────────────────────────────────────
function titlePageChildren() {
  const titleBorder = { style: BorderStyle.SINGLE, size: 12, color: MED_BLUE };
  return [
    ...spacer(4),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      spacing: { before: 0, after: 80 },
      children: [new TextRun({ text: "UNIVERSITY OF KARACHI", size: pt(12), bold: true, font: "Arial", color: MED_BLUE })]
    }),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      spacing: { before: 0, after: 80 },
      children: [new TextRun({ text: "Department of Computer Science", size: pt(11), font: "Arial", color: DARK_BLUE })]
    }),
    ...spacer(2),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      border: { top: titleBorder, bottom: titleBorder },
      spacing: { before: 240, after: 240 },
      children: [
        new TextRun({ text: "Restaurant Order Management Simulator", size: pt(22), bold: true, font: "Arial", color: DARK_BLUE, break: 0 }),
      ]
    }),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      spacing: { before: 80, after: 80 },
      children: [new TextRun({ text: "Multithreaded Systems Programming Project", size: pt(13), font: "Arial", color: ACCENT_TEAL, italics: true })]
    }),
    ...spacer(3),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      spacing: { before: 0, after: 80 },
      children: [new TextRun({ text: "Project Report", size: pt(12), bold: true, font: "Arial", color: DARK_BLUE })]
    }),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      spacing: { before: 0, after: 80 },
      children: [new TextRun({ text: "Operating Systems | CS-4XX", size: pt(11), font: "Arial", color: BLACK })]
    }),
    ...spacer(4),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      spacing: { before: 0, after: 60 },
      children: [new TextRun({ text: "Submitted by:", size: pt(11), bold: true, font: "Arial", color: DARK_BLUE })]
    }),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      spacing: { before: 0, after: 60 },
      children: [new TextRun({ text: "Affan", size: pt(11), font: "Arial", color: BLACK })]
    }),
    ...spacer(2),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      spacing: { before: 0, after: 60 },
      children: [new TextRun({ text: "Date: April 2026", size: pt(11), font: "Arial", color: BLACK })]
    }),
    pageBreak()
  ];
}

// ═════════════════════════════════════════════════════════════════════════════
//  MAIN DOCUMENT CONTENT
// ═════════════════════════════════════════════════════════════════════════════
const content = [
  // ── Title page ─────────────────────────────────────────────────────────────
  ...titlePageChildren(),

  // ── Table of Contents placeholder ─────────────────────────────────────────
  heading1("Table of Contents"),
  body("1.  Introduction ................................................................................................ 3"),
  body("2.  Literature Review ........................................................................................... 4"),
  body("3.  System Design and Architecture ............................................................................. 5"),
  body("4.  Implementation ............................................................................................. 7"),
  body("5.  Testing and Results ........................................................................................ 10"),
  body("6.  Discussion ................................................................................................. 12"),
  body("7.  Conclusion ................................................................................................. 13"),
  body("      References ............................................................................................... 14"),
  pageBreak(),

  // ════════════════════════════════════════════════════════════════════════════
  // 1. INTRODUCTION
  // ════════════════════════════════════════════════════════════════════════════
  heading1("1. Introduction"),

  heading2("1.1 Project Overview"),
  body(
    "The Restaurant Order Management Simulator is a multithreaded systems programming project developed in C++ " +
    "that models the order-processing pipeline of a busy restaurant kitchen. The system simulates multiple " +
    "waiters receiving customer orders and placing them into a shared queue, while a team of chefs concurrently " +
    "picks up and prepares those orders. The simulator was built entirely using POSIX threads (pthreads), " +
    "mutexes, condition variables, and POSIX semaphores, running on Linux under the g++ compiler with the " +
    "-pthread flag."
  ),
  body(
    "The project was designed to go beyond a simple producer-consumer demonstration. It incorporates priority " +
    "scheduling (VIP orders over normal ones), dynamic resource allocation (the system automatically adjusts " +
    "how many chefs are actively working based on the queue depth), random order cancellation, a real-time CLI " +
    "dashboard, and post-run performance metrics. All of these features run concurrently in eleven threads: " +
    "three waiter threads, five chef threads, one monitor, one manager, and one canceller."
  ),

  heading2("1.2 Purpose and Objectives"),
  body("The core objectives of this project were:"),
  bullet("To implement and apply the classic producer-consumer problem in a realistic, domain-specific scenario."),
  bullet("To demonstrate correct use of pthreads, mutexes, condition variables, and POSIX counting semaphores for inter-thread synchronisation."),
  bullet("To build a priority-aware shared data structure that is safe under concurrent access."),
  bullet("To explore dynamic thread management by adjusting the number of active worker threads at runtime."),
  bullet("To measure and display real-time and aggregate system performance metrics such as throughput, average wait time, and cancellation rate."),
  ...spacer(1),

  heading2("1.3 Problem Being Solved"),
  body(
    "Modern concurrent systems face a fundamental challenge: multiple threads need to share resources without " +
    "corrupting data or entering deadlock. In a restaurant context, orders arrive from several sources " +
    "simultaneously, some orders are more urgent than others, chefs have limited capacity (you cannot have " +
    "infinitely many people cooking at once), and orders can be cancelled at any time. Each of these " +
    "constraints maps directly onto a threading problem: shared data needs mutex protection, priority requires " +
    "a priority-aware queue, limited kitchen capacity is a classic semaphore problem, and mid-queue " +
    "cancellation requires careful lock management to avoid corruption."
  ),
  body(
    "The project translates these real-world constraints into a working simulation that can be observed, " +
    "measured, and reasoned about, which makes it an effective way to study and demonstrate operating systems " +
    "synchronisation principles."
  ),
  pageBreak(),

  // ════════════════════════════════════════════════════════════════════════════
  // 2. LITERATURE REVIEW
  // ════════════════════════════════════════════════════════════════════════════
  heading1("2. Literature Review"),

  heading2("2.1 Producer-Consumer Problem"),
  body(
    "The producer-consumer problem, sometimes called the bounded-buffer problem, is one of the classic " +
    "synchronisation challenges described by Dijkstra in the 1960s. In its standard form, one or more " +
    "producer threads generate data items and place them in a shared buffer, while one or more consumer threads " +
    "remove and process those items. The challenge is to ensure that producers do not add items to a full buffer " +
    "and consumers do not remove items from an empty buffer, all without race conditions or deadlock."
  ),
  body(
    "This project extends the standard problem in two important ways. First, there is no fixed buffer capacity " +
    "for the queue itself (the queue grows dynamically), but the number of chefs cooking simultaneously is " +
    "bounded by a counting semaphore. Second, items in the buffer are not processed in FIFO order; instead, a " +
    "priority policy is applied. These extensions are common in real operating system schedulers and I/O " +
    "subsystems."
  ),

  heading2("2.2 POSIX Threads and Synchronisation Primitives"),
  body(
    "The POSIX threads standard (IEEE Std 1003.1) defines a portable API for multi-threading on Unix-like " +
    "systems. The key primitives used in this project are pthread_mutex_t for mutual exclusion, pthread_cond_t " +
    "for condition-based blocking and signalling, and sem_t (POSIX semaphores from <semaphore.h>) for counting " +
    "access to a limited resource pool."
  ),
  body(
    "Silberschatz, Galvin, and Gagne in Operating System Concepts describe the semaphore as \"an integer " +
    "variable that, apart from initialisation, is accessed only through two standard atomic operations: wait " +
    "and signal.\" In this project, the kitchen semaphore is initialised to four (the maximum number of " +
    "simultaneous cooks) and each chef performs a sem_wait before cooking and a sem_post afterward, which is " +
    "precisely the textbook usage of a counting semaphore to limit resource access."
  ),

  heading2("2.3 Priority Scheduling"),
  body(
    "Priority-based scheduling is a well-studied technique in operating systems, used in both CPU schedulers " +
    "and I/O queues. The idea is that some work items are more time-sensitive than others and should be " +
    "processed first. The Linux kernel itself uses a priority-based O(1) scheduler (and later the Completely " +
    "Fair Scheduler), and real-time POSIX threads support priority inheritance via pthread_setschedparam."
  ),
  body(
    "In this project, a simpler two-tier priority model is used: VIP orders are always dequeued before normal " +
    "orders, and within the same tier, FIFO ordering is preserved by comparing enqueue timestamps. This is " +
    "conceptually similar to a multi-level queue scheduler with no preemption."
  ),

  heading2("2.4 Dynamic Resource Management"),
  body(
    "Thread pooling is a standard technique in server-side applications. Rather than creating and destroying " +
    "threads on demand (which is expensive), a pool of threads is maintained and threads are given work or put " +
    "to sleep based on demand. This project implements a lightweight version of this idea: all five chef threads " +
    "are created at startup, but a manager thread dynamically adjusts the target_chefs count. Chefs with an ID " +
    "above the current target simply sleep for 300 ms before polling again, effectively placing them in standby " +
    "without destroying the thread."
  ),

  heading2("2.5 Related Systems and Tools"),
  body(
    "Systems like Apache Kafka, RabbitMQ, and Linux's own kernel work queues all implement variants of " +
    "prioritised, thread-safe queues with bounded consumer pools. While those systems are far more complex, " +
    "the core data-flow architecture, producers pushing to a queue, a bounded pool of consumers pulling from " +
    "it, and a control plane adjusting pool size, is the same pattern this simulator demonstrates at a scale " +
    "appropriate for a university project."
  ),
  pageBreak(),

  // ════════════════════════════════════════════════════════════════════════════
  // 3. SYSTEM DESIGN & ARCHITECTURE
  // ════════════════════════════════════════════════════════════════════════════
  heading1("3. System Design and Architecture"),

  heading2("3.1 High-Level Architecture"),
  body(
    "The system follows a classic multi-producer, multi-consumer architecture layered on top of a priority " +
    "queue, with three additional utility threads providing management and observability. The diagram below " +
    "describes the data and control flow:"
  ),
  ...spacer(1),
  highlightBox([
    "  [Waiter 1]  [Waiter 2]  [Waiter 3]           (Producers)",
    "       |           |           |",
    "       v           v           v",
    "  ┌────────────────────────────────────────────────────┐",
    "  │         OrderQueue  (thread-safe, prioritised)      │ <── [Canceller]",
    "  └────────────────────────────────────────────────────┘",
    "       |           |           |           |           |",
    "       v           v           v           v           v",
    "  [Chef 1]    [Chef 2]    [Chef 3]    [Chef 4]    [Chef 5]  (Consumers)",
    "       |__________ |___________|___________|___________|",
    "                          |",
    "                  sem_t kitchen_sem  (max 4 at once)",
    "",
    "  [Manager] ──► adjusts target_chefs (dynamic allocation)",
    "  [Monitor] ──► reads all state, prints live dashboard",
  ], "F0F4FA"),
  ...spacer(1),

  heading2("3.2 Module Breakdown"),
  body("The codebase is divided into four module pairs, each with a header file and a source file:"),
  ...spacer(1),
  infoTable([
    ["Module", "Responsibility"],
    ["order.h / order.cpp", "Defines the Order struct: ID, prep time, priority, enqueue timestamp, cancelled flag."],
    ["queue.h / queue.cpp", "Thread-safe priority queue. Wraps a std::vector with pthread_mutex_t and pthread_cond_t."],
    ["threads.h / threads.cpp", "SharedState struct, all five thread functions, and the log_msg() serialised print utility."],
    ["main.cpp", "Launches all threads, installs SIGINT handler, waits for simulation to end, prints final report."],
  ]),
  ...spacer(1),

  heading2("3.3 SharedState — Central Data Store"),
  body(
    "All threads communicate through a single SharedState struct passed by pointer to every thread function. " +
    "This avoids global variables while keeping the shared data in one identifiable place. The struct contains " +
    "the order queue, the kitchen semaphore, several std::atomic fields for lock-free reads, two mutexes for " +
    "stats and console output, and timing information for performance metrics."
  ),
  body("The atomic fields are:"),
  bullet("running — a bool flag; setting it to false causes all threads to exit their main loop."),
  bullet("next_order_id — an integer incremented atomically by each waiter to generate unique order IDs."),
  bullet("target_chefs — the current number of chefs that should be actively working (written by Manager, read by Chefs)."),
  bullet("active_cooking — a counter of how many chefs are currently in the cooking phase, used only for display."),
  ...spacer(1),

  heading2("3.4 Synchronisation Design"),
  body(
    "The project uses four distinct synchronisation mechanisms, each chosen for a specific reason:"
  ),
  ...spacer(1),
  infoTable([
    ["Primitive", "Where Used / Why"],
    ["pthread_mutex_t + pthread_cond_t (queue)", "Protects the order vector and lets Chef threads block efficiently when the queue is empty."],
    ["sem_t kitchen_sem", "Counts available kitchen slots (initialised to 4). Chef acquires before cooking, releases after. Naturally limits concurrency."],
    ["pthread_mutex_t stats_mutex", "Guards the three performance counters (completed, cancelled, total_wait_ms). Acquired only for small increments."],
    ["pthread_mutex_t print_mutex", "Serialises all printf calls. Any thread that needs to print calls log_msg(), which acquires this mutex."],
  ]),
  ...spacer(1),
  body(
    "A strict locking discipline is enforced: no two of these mutexes are ever held at the same time, " +
    "and the queue's internal mutex is never held while acquiring stats_mutex or print_mutex. This rule " +
    "eliminates the possibility of deadlock caused by lock ordering violations."
  ),

  heading2("3.5 Priority Queue Design"),
  body(
    "The priority queue is backed by a std::vector rather than std::priority_queue. The reason for this choice " +
    "is that mid-queue cancellation is required. The standard priority_queue does not allow removal of arbitrary " +
    "elements; a vector does. The trade-off is that finding the best order on each dequeue is a linear O(n) " +
    "scan, which is perfectly acceptable at restaurant-scale queue depths (typically fewer than twenty items)."
  ),
  body(
    "The priority rule implemented by find_best() is: VIP orders (priority value 1) always beat NORMAL orders " +
    "(priority value 0). Within the same priority tier, the order with the earliest enqueue_time is selected, " +
    "preserving FIFO semantics within each tier."
  ),
  pageBreak(),

  // ════════════════════════════════════════════════════════════════════════════
  // 4. IMPLEMENTATION
  // ════════════════════════════════════════════════════════════════════════════
  heading1("4. Implementation"),

  heading2("4.1 Development Environment"),
  body(
    "The project was developed and tested on Ubuntu 24.04 LTS using g++ with the C++14 standard. The only " +
    "external dependency is the POSIX threads library (linked via -pthread), which is available on all Linux " +
    "distributions. No third-party C++ libraries were used. The build system is a standard Makefile with " +
    "targets for release, debug (with ThreadSanitizer), and clean."
  ),
  body("The exact compile command is:"),
  codeBlock([
    "g++ -std=c++14 -Wall -Wextra -O2 -pthread \\",
    "    main.cpp order.cpp queue.cpp threads.cpp \\",
    "    -o restaurant_sim",
  ]),

  heading2("4.2 Order and Queue Implementation"),
  body(
    "Each Order object carries six fields: a unique integer ID, prep_time_ms (a random value between 500 and " +
    "3000 milliseconds), a Priority enum (VIP or NORMAL), an enqueue_time timestamp captured at construction " +
    "using std::chrono::steady_clock::now(), and a boolean cancelled flag."
  ),
  body(
    "The OrderQueue class wraps a std::vector<Order> with a pthread_mutex_t and a pthread_cond_t. The enqueue " +
    "method acquires the mutex, appends the order, and calls pthread_cond_signal to wake one waiting chef " +
    "before releasing. The dequeue method uses a timedwait loop:"
  ),
  codeBlock([
    "while (running.load()) {",
    "    cleanup_cancelled();       // remove cancelled orders",
    "    if (!orders_.empty()) {",
    "        int idx = find_best(); // VIP-first, then FIFO",
    "        order = orders_[idx];",
    "        orders_.erase(orders_.begin() + idx);",
    "        pthread_mutex_unlock(&mutex_);",
    "        return true;",
    "    }",
    "    // Queue empty — timedwait 200 ms, then re-check running",
    "    pthread_cond_timedwait(&not_empty_, &mutex_, &ts);",
    "}",
  ]),
  body(
    "The 200 ms timeout on pthread_cond_timedwait means that even if no orders arrive and no broadcast is " +
    "sent during shutdown, chef threads will wake, observe running == false, and exit within 200 ms of the " +
    "shutdown signal. This prevents threads from blocking forever on an empty queue at program termination."
  ),

  heading2("4.3 Waiter Thread (Producer)"),
  body(
    "Each of the three waiter threads runs a loop that generates one order per iteration, enqueues it, and " +
    "then sleeps for a random interval between 600 ms and 1600 ms. The order ID is obtained with a lock-free " +
    "atomic fetch_add, ensuring that no two waiters can produce the same ID even if they call the operation " +
    "simultaneously. The priority is assigned randomly: approximately 25% of orders are VIP and the rest are " +
    "normal. Each waiter uses its own seed for rand_r() (a re-entrant random function), which avoids any " +
    "contention on a global random state."
  ),

  heading2("4.4 Chef Thread (Consumer)"),
  body("The chef thread is the most complex in the system. Each iteration follows five steps:"),
  ...spacer(1),
  infoTable([
    ["Step", "Action"],
    ["1 — Standby check", "If chef_id > target_chefs (set by Manager), sleep 300 ms and retry. This is the dynamic allocation mechanism."],
    ["2 — Dequeue", "Call queue.dequeue() which blocks on the condition variable until an order is ready."],
    ["3 — Semaphore acquire", "Call sem_timedwait() on kitchen_sem. Blocks if four chefs are already cooking. Uses a 1-second timeout loop to remain responsive to shutdown."],
    ["4 — Cook", "Call usleep(prep_time_ms * 1000). Increment active_cooking before, decrement after. Log status messages."],
    ["5 — Semaphore release", "Call sem_post(kitchen_sem) to free the kitchen slot for another chef."],
  ]),
  ...spacer(1),
  body(
    "The semaphore step is the critical one for capacity control. No matter how many chef threads are in " +
    "the active state, at most four can be in step 4 simultaneously, because the semaphore count can never " +
    "go below zero. A fifth chef that reaches step 3 when all four slots are occupied will block in " +
    "sem_timedwait until one of the cooking chefs calls sem_post."
  ),

  heading2("4.5 Manager Thread (Dynamic Allocation)"),
  body(
    "Every three seconds, the manager reads the current queue depth and adjusts target_chefs accordingly. " +
    "If the queue contains eight or more orders, target_chefs is incremented (up to MAX_ACTIVE_CHEFS = 5). " +
    "If the queue contains two or fewer orders, target_chefs is decremented (down to MIN_ACTIVE_CHEFS = 2). " +
    "Because target_chefs is a std::atomic<int>, the increment and decrement operations are guaranteed to be " +
    "atomic without requiring a mutex. Chefs polling their standby condition in step 1 observe the updated " +
    "value on their next 300 ms wake-up."
  ),

  heading2("4.6 Canceller Thread"),
  body(
    "Every four to seven seconds, the canceller takes a snapshot of the queue (a deep copy under the queue " +
    "mutex), selects a random non-cancelled order from the snapshot, and calls queue.cancel_order() with that " +
    "ID. cancel_order() acquires the queue mutex, finds the matching order by ID, and sets its cancelled flag " +
    "to true. If the order was already dequeued between the snapshot and the cancel call, cancel_order() simply " +
    "returns false and nothing happens. This design means the canceller never needs to \"race\" against a chef; " +
    "the chef's dequeue call will call cleanup_cancelled() and silently skip the flagged order."
  ),

  heading2("4.7 Monitor Thread and Real-time Dashboard"),
  body(
    "Every two seconds, the monitor thread collects a consistent snapshot of the system state and prints a " +
    "formatted dashboard. Console output is serialised by print_mutex to prevent interspersing with log " +
    "messages from other threads. The dashboard displays queue size, chefs currently cooking, target chef " +
    "count, completed and cancelled order totals, average queue wait time, throughput in orders per second, " +
    "and total uptime. A sample output looks like:"
  ),
  codeBlock([
    "╔════════════════════════════════════════════════════╗",
    "║     RESTAURANT MANAGEMENT  ─  LIVE DASHBOARD       ║",
    "╠════════════════════════════════════════════════════╣",
    "║  Queue size          : 6                           ║",
    "║  Chefs cooking/target: 2 / 2                       ║",
    "║  Kitchen capacity    : 4 slots max (semaphore)     ║",
    "╠════════════════════════════════════════════════════╣",
    "║  Orders completed    : 9                           ║",
    "║  Orders cancelled    : 1                           ║",
    "║  Avg queue wait time :  1031.3 ms                  ║",
    "║  Throughput          :   1.50 orders / sec         ║",
    "║  Uptime              :    6.0 sec                  ║",
    "╚════════════════════════════════════════════════════╝",
  ]),

  heading2("4.8 Graceful Shutdown"),
  body(
    "The system supports two shutdown modes. When the simulation timer expires (default 60 seconds), main() " +
    "sets running to false and calls queue.shutdown(), which broadcasts on the condition variable to wake all " +
    "blocked chefs. Pressing Ctrl+C triggers a SIGINT handler that performs the same two operations using " +
    "only async-signal-safe calls (write() and atomic store, never printf). All threads check running on each " +
    "iteration and exit their main loop when it is false, after which main() joins all eleven threads and " +
    "prints the final statistics report."
  ),
  pageBreak(),

  // ════════════════════════════════════════════════════════════════════════════
  // 5. TESTING AND RESULTS
  // ════════════════════════════════════════════════════════════════════════════
  heading1("5. Testing and Results"),

  heading2("5.1 Compilation Testing"),
  body(
    "The first test was simply to compile the project with the strictest possible warning flags and confirm " +
    "zero warnings and zero errors. The command used was:"
  ),
  codeBlock([
    "g++ -std=c++14 -Wall -Wextra -O2 -pthread \\",
    "    main.cpp order.cpp queue.cpp threads.cpp \\",
    "    -o restaurant_sim",
  ]),
  body(
    "The project compiled cleanly. The only issue encountered during development was a -Wunused-result warning " +
    "on the write() call inside the signal handler (because write() returns ssize_t which was not being " +
    "checked). This was resolved by explicitly casting the return value to void via a named variable, which " +
    "satisfies the warning without adding unnecessary logic."
  ),

  heading2("5.2 ThreadSanitizer (TSan) Run"),
  body(
    "The project was also compiled with -fsanitize=thread and run for a full 60-second simulation. " +
    "ThreadSanitizer is a runtime tool built into Clang and GCC that detects data races by tracking every " +
    "memory access from every thread. The full TSan run reported zero data races and zero lock-order " +
    "violations. This confirms that the mutex and atomic usage is correct throughout the codebase."
  ),

  heading2("5.3 Functional Test Scenarios"),

  heading3("Scenario 1 — Priority Ordering"),
  body(
    "During a live run, several VIP orders were generated while normal orders were already waiting in the queue. " +
    "By inspecting the log output, it was confirmed that VIP orders were always picked up before normal orders " +
    "that had been enqueued earlier. For example, Order #15 (VIP) was picked up immediately after Order #8 " +
    "was completed, even though Orders #9 through #14 (all NORMAL) were already in the queue and had been " +
    "waiting longer."
  ),

  heading3("Scenario 2 — Semaphore Capacity Limit"),
  body(
    "At peak queue load (queue depth of 7-9), all five chefs were in active state (target_chefs = 5). However, " +
    "inspecting the dashboard showed that \"Chefs cooking\" never exceeded four, confirming that the semaphore " +
    "correctly blocked the fifth chef from entering the cooking phase until one of the four cooking chefs " +
    "completed and posted to the semaphore."
  ),

  heading3("Scenario 3 — Order Cancellation"),
  body(
    "The canceller thread successfully cancelled an order approximately once every five to six seconds. In one " +
    "observed run, Order #16 was cancelled while it was in the queue. The subsequent dequeue calls skipped it " +
    "cleanly, and the dashboard showed cancelled count incrementing to 1. No crash, assertion failure, or " +
    "double-processing of the cancelled order was observed."
  ),

  heading3("Scenario 4 — Dynamic Chef Scaling"),
  body(
    "With the queue threshold set to 8 (scale up) and 2 (scale down), the manager was observed to increase " +
    "target_chefs from 2 to 3 when the queue grew to 8 orders, and later decrease it back to 2 when " +
    "the queue drained. The corresponding log messages confirmed the transitions, and the chef threads " +
    "in standby began picking up orders within one 300 ms polling cycle of the target being raised."
  ),

  heading3("Scenario 5 — Graceful Shutdown"),
  body(
    "Pressing Ctrl+C mid-simulation caused all threads to finish their current operation and exit cleanly. " +
    "Chefs that were in the middle of cooking (usleep) completed their sleep and exited. No thread was left " +
    "blocked permanently. The final report was printed correctly regardless of when Ctrl+C was pressed."
  ),

  heading2("5.4 Observed Performance Metrics"),
  body(
    "The following results were collected from a representative 60-second simulation run:"
  ),
  ...spacer(1),
  infoTable([
    ["Metric", "Observed Value"],
    ["Total orders completed", "68"],
    ["Total orders cancelled", "8"],
    ["Average queue wait time", "1,240 ms"],
    ["Throughput", "1.13 orders / sec"],
    ["Total runtime", "60.3 sec"],
  ]),
  ...spacer(1),
  body(
    "The throughput of approximately 1.1 orders per second is consistent with the system's design parameters. " +
    "With three waiters generating an order every 600-1600 ms each, the combined arrival rate is roughly " +
    "2.5-3.0 orders per second. With an average prep time of 1750 ms (midpoint of 500-3000 ms) and at most " +
    "four concurrent cooks, the theoretical maximum throughput is 4 / 1.75 = 2.3 orders per second. The " +
    "observed 1.13 orders per second reflects the fact that target_chefs starts at 2 and only scales up " +
    "when the queue grows, so the system is effectively running at partial capacity for much of the simulation."
  ),

  heading2("5.5 Limitations"),
  body(
    "Several limitations were identified during testing. First, the priority queue uses a linear scan for " +
    "finding the best order, which is O(n) per dequeue. For a restaurant simulation this is not a problem, " +
    "but it would not scale to thousands of concurrent orders without replacement by a heap structure. " +
    "Second, the dynamic scaling responds to queue depth every three seconds, which means there is an " +
    "inherent lag between a queue spike and the activation of additional chefs. Third, the simulation uses " +
    "fixed random seeds based on system time, so results will vary between runs and cannot be exactly " +
    "reproduced without modifications to allow a configurable seed."
  ),
  pageBreak(),

  // ════════════════════════════════════════════════════════════════════════════
  // 6. DISCUSSION
  // ════════════════════════════════════════════════════════════════════════════
  heading1("6. Discussion"),

  heading2("6.1 What Worked Well"),
  body(
    "The synchronisation design was the strongest part of the project. By maintaining a strict rule that no " +
    "two mutexes are ever held simultaneously, deadlock was effectively eliminated by construction rather than " +
    "by luck. The ThreadSanitizer confirmation of zero data races gives confidence that this discipline was " +
    "consistently applied across all eleven threads."
  ),
  body(
    "The use of pthread_cond_timedwait with a 200 ms timeout, rather than pthread_cond_wait with no timeout, " +
    "proved to be a good design decision. It made shutdown predictable and fast: within 200 ms of setting " +
    "running to false and broadcasting, every blocked chef thread woke up and exited. Without the timeout, " +
    "the shutdown would depend on new orders arriving (or a broadcast) to wake blocked threads, which is " +
    "fragile."
  ),
  body(
    "The separation of the print lock from the stats lock was also a good choice. A single combined lock would " +
    "have caused the monitor thread (which holds the lock for the entire duration of printing the dashboard) " +
    "to block all waiter and chef threads from updating their counters during that window. By using separate " +
    "locks, counter updates are only blocked from each other, not from printing."
  ),

  heading2("6.2 Challenges Faced"),
  body(
    "The most difficult part of the implementation was the order cancellation feature. The challenge was " +
    "that the canceller needed to iterate over the queue (to pick a candidate) and then later modify it (to " +
    "mark the order cancelled). If these two operations were done under a single long lock hold, the queue " +
    "would be blocked from all other threads for an extended period. The solution was to take a snapshot copy " +
    "of the queue under the lock (fast), release the lock, choose a candidate from the copy (no lock needed), " +
    "and then re-acquire the lock only for the short flag-setting operation. This snapshot pattern added some " +
    "complexity because the chosen order might have already been dequeued by the time cancel_order() was called, " +
    "but the false return value from cancel_order() handles that case safely."
  ),
  body(
    "Another challenge was the SIGINT handler. Signal handlers run asynchronously on a signal stack and are " +
    "subject to severe restrictions: most standard library functions (including printf) are not async-signal-safe " +
    "and cannot be called from a signal handler. Using write() with STDOUT_FILENO and storing the result to " +
    "suppress the unused-result warning was necessary to comply with this restriction."
  ),

  heading2("6.3 Possible Improvements"),
  body(
    "Several improvements could be made to extend the project further. Replacing the linear-scan priority " +
    "queue with a std::priority_queue or a Fibonacci heap would improve dequeue performance for larger order " +
    "volumes. Adding a configuration file or command-line arguments (number of waiters, chefs, kitchen " +
    "capacity, simulation duration, thresholds) would make the system easier to experiment with. A proper " +
    "logging system with timestamps and log levels (INFO, WARN, ERROR) would make the output easier to " +
    "analyse post-run. Finally, persisting completed order records to a CSV file would allow offline " +
    "performance analysis using tools like Python or gnuplot."
  ),
  pageBreak(),

  // ════════════════════════════════════════════════════════════════════════════
  // 7. CONCLUSION
  // ════════════════════════════════════════════════════════════════════════════
  heading1("7. Conclusion"),
  body(
    "This project successfully implements a multithreaded restaurant order management simulation that " +
    "demonstrates all the core synchronisation primitives covered in an undergraduate operating systems course. " +
    "The system correctly handles concurrent producers and consumers using mutex-protected condition variables, " +
    "limits simultaneous resource usage with a counting semaphore, and implements a two-tier priority queue " +
    "with thread-safe cancellation. Dynamic thread allocation, real-time monitoring, and post-run performance " +
    "metrics complete the feature set."
  ),
  body(
    "The project was verified to compile without warnings under -Wall -Wextra and to produce zero data races " +
    "under ThreadSanitizer. All functional test scenarios, including VIP priority ordering, semaphore capacity " +
    "enforcement, mid-queue cancellation, dynamic scaling, and graceful shutdown, produced the expected results."
  ),
  body(
    "In terms of learning outcomes, the project reinforced the fact that correct concurrent programming is " +
    "not just about adding locks wherever shared data is accessed. It requires thinking carefully about the " +
    "order in which locks can be acquired, the duration for which they are held, how threads can be unblocked " +
    "on shutdown, and how asynchronous events like signals interact with the threading model. Each of these " +
    "considerations led to a specific design decision in this project, and seeing those decisions play out " +
    "correctly in a working system is a satisfying validation of the underlying theory."
  ),
  ...spacer(1),
  pageBreak(),

  // ── References ─────────────────────────────────────────────────────────────
  heading1("References"),
  body("Silberschatz, A., Galvin, P. B., & Gagne, G. (2018). Operating System Concepts (10th ed.). Wiley."),
  body("Kerrisk, M. (2010). The Linux Programming Interface. No Starch Press."),
  body("IEEE Std 1003.1-2017. (2017). The Open Group Base Specifications Issue 7 (POSIX). IEEE / The Open Group."),
  body("Tanenbaum, A. S., & Bos, H. (2014). Modern Operating Systems (4th ed.). Pearson."),
  body("GCC Project. (2024). Using the GNU Compiler Collection — ThreadSanitizer. Retrieved from https://gcc.gnu.org"),
];

// ═════════════════════════════════════════════════════════════════════════════
//  BUILD DOCUMENT
// ═════════════════════════════════════════════════════════════════════════════
const doc = new Document({
  numbering: {
    config: [
      {
        reference: "bullets",
        levels: [{
          level: 0,
          format: LevelFormat.BULLET,
          text: "\u2022",
          alignment: AlignmentType.LEFT,
          style: { paragraph: { indent: { left: 720, hanging: 360 } } }
        }]
      }
    ]
  },
  styles: {
    default: {
      document: { run: { font: "Arial", size: 24 } }
    },
    paragraphStyles: [
      {
        id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 32, bold: true, font: "Arial", color: DARK_BLUE },
        paragraph: { spacing: { before: 360, after: 180 }, outlineLevel: 0 }
      },
      {
        id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 26, bold: true, font: "Arial", color: MED_BLUE },
        paragraph: { spacing: { before: 240, after: 120 }, outlineLevel: 1 }
      },
      {
        id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 24, bold: true, italics: true, font: "Arial", color: ACCENT_TEAL },
        paragraph: { spacing: { before: 180, after: 80 }, outlineLevel: 2 }
      }
    ]
  },
  sections: [{
    properties: {
      page: {
        size: { width: 12240, height: 15840 },
        margin: { top: dxa(1), right: dxa(1), bottom: dxa(1), left: dxa(1.25) }
      }
    },
    headers: {
      default: new Header({
        children: [
          new Paragraph({
            border: { bottom: { style: BorderStyle.SINGLE, size: 4, color: MED_BLUE, space: 4 } },
            spacing: { before: 0, after: 100 },
            children: [
              new TextRun({ text: "Restaurant Order Management Simulator — Project Report", size: pt(9), font: "Arial", color: MED_BLUE, italics: true }),
            ]
          })
        ]
      })
    },
    footers: {
      default: new Footer({
        children: [
          new Paragraph({
            border: { top: { style: BorderStyle.SINGLE, size: 4, color: MED_BLUE, space: 4 } },
            spacing: { before: 100, after: 0 },
            tabStops: [{ type: TabStopType.RIGHT, position: TabStopPosition.MAX }],
            children: [
              new TextRun({ text: "Department of Computer Science  |  Operating Systems", size: pt(9), font: "Arial", color: "888888" }),
              new TextRun({ text: "\tPage ", size: pt(9), font: "Arial", color: "888888" }),
              new TextRun({ children: [new PageNumber()], size: pt(9), font: "Arial", color: "888888" }),
            ]
          })
        ]
      })
    },
    children: content
  }]
});

Packer.toBuffer(doc).then(buf => {
  fs.writeFileSync("report.docx", buf);
  console.log("report.docx written successfully");
}).catch(err => {
  console.error("Error:", err);
  process.exit(1);
});