# See the file LICENSE for redistribution information.
#
# Copyright (c) 2000-2003
#	Sleepycat Software.  All rights reserved.
#
# $Id: test091.tcl,v 11.9 2003/01/08 05:54:09 bostic Exp $
#
# TEST	test091
# TEST	Test of DB_CONSUME_WAIT.
proc test091 { method {nconsumers 4} \
    {nproducers 2} {nitems 1000} {start 0 } {tnum "091"} args} {
	if { [is_queue $method ] == 0 } {
		puts "Skipping test0$tnum for $method."
		return;
	}
	eval {test070 $method \
	    $nconsumers $nproducers $nitems WAIT $start -txn $tnum } $args
	eval {test070 $method \
	    $nconsumers $nproducers $nitems WAIT $start -cdb $tnum } $args
}
