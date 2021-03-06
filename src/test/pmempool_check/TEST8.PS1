#
# Copyright 2014-2017, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#
#     * Neither the name of the copyright holder nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#
# pmempool_check/TEST8 -- test for checking pools
#
[CmdletBinding(PositionalBinding=$false)]
Param(
    [alias("d")]
    $DIR = ""
    )
$Env:UNITTEST_NAME = "pmempool_check/TEST8"
$Env:UNITTEST_NUM = "8"

. ..\unittest\unittest.ps1

require_test_type medium

require_fs_type any

setup

$POOLSET="$DIR\pool.set"
$POOL_P1="$DIR\pool.p1"
$POOL_P2="$DIR\pool.p2"
$POOL_P3="$DIR\pool.p3"
$POOL_P4="$DIR\pool.p4"

$LOG="out$Env:UNITTEST_NUM.log"
rm $LOG -Force -ea si

create_poolset $POOLSET `
	32M:$POOL_P1 `
	32M:$POOL_P2 `
	32M:$POOL_P3 `
	32M:$POOL_P4

check_file $POOLSET

rm $POOL_P1, $POOL_P2, $POOL_P3, $POOL_P4 -Force -ea si

expect_normal_exit $PMEMPOOL create blk 512 $POOLSET
expect_normal_exit $PMEMPOOL info $POOLSET > $null
check_files $POOL_P1 $POOL_P2 $POOL_P3 $POOL_P4
&$PMEMSPOIL -v $POOL_P1 `
	pool_hdr.next_part_uuid=ERROR_NEXT `
	pool_hdr.prev_part_uuid=ERROR_PREV `
	pool_hdr.next_repl_uuid=ERROR_NEXT_REP `
	pool_hdr.prev_repl_uuid=ERROR_PREV_REP `
	pool_hdr.poolset_uuid=ERROR `
	>> $LOG

expect_abnormal_exit $PMEMPOOL check $POOLSET >> $LOG
expect_normal_exit $PMEMPOOL check -vyr $POOLSET >> $LOG
expect_normal_exit $PMEMPOOL check $POOLSET >> $LOG
expect_normal_exit $PMEMPOOL info $POOLSET > $null

check

pass
