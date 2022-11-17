// Licensed to Apache Software Foundation (ASF) under one or more contributor
// license agreements. See the NOTICE file distributed with
// this work for additional information regarding copyright
// ownership. Apache Software Foundation (ASF) licenses this file to you under
// the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

package host

import (
	"fmt"
	"time"

	v3 "skywalking.apache.org/repo/goapi/collect/common/v3"

	"github.com/shirou/gopsutil/host"
)

// BootTime the System boot time
var BootTime time.Time

func init() {
	boot, err := host.BootTime()
	if err != nil {
		panic(fmt.Errorf("init boot time error: %v", err))
	}
	BootTime = time.Unix(int64(boot), 0)
}

func TimeToInstant(bpfTime uint64) *v3.Instant {
	timeCopy := time.Unix(BootTime.Unix(), int64(BootTime.Nanosecond()))
	result := timeCopy.Add(time.Duration(bpfTime))
	return &v3.Instant{
		Seconds: result.Unix(),
		Nanos:   int32(result.Nanosecond()),
	}
}
