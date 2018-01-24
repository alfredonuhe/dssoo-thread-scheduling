# dssoo-thread-scheduling
C implementation of three thread schedulers. The strategies used by the schedulers are descrbided below:

  - RR.c: thread scheduler using a simple Round-Robin strategy.
  - RRF.c: thread scheeduler using a Round-Robin strategy for low priority threads and a FIFO strategy for high priority threads with expulsion.
  - RRFI.c: thread scheduling equal to "RRF.c", except for a priority increase with thread starvation.
