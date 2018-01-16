package main

import (
	"fmt"
	"github.com/influxdata/influxdb/client/v2"
	"io/ioutil"
	"os"
	"strconv"
	"strings"
	"time"
)

const KB = 1024
const MB = 1024 * KB
const GB = 1024 * MB

var MemInfoKeys = []string{
	"MemFree",
	"MemAvailable",
	"Buffers",
	"Cached",
	"SwapCached",
	"Active",
	"Inactive",
	"Active(anon)",
	"Inactive(anon)",
	"Active(file)",
	"Inactive(file)",
	"Unevictable",
	"Mlocked",
	"SwapFree",
	"Dirty",
	"Writeback",
	"AnonPages",
	"Mapped",
	"Shmem",
	"Slab",
	"SReclaimable",
	"SUnreclaim",
	"KernelStack",
	"PageTables",
	"CommitLimit",
	"Committed_AS",
}

func PushMemInfos(d DataSource) error {
	p1, err := FetchMemInfo()
	if err != nil {
		return err
	}
	p2, err := FetchCPUUsage()
	if err != nil {
		return err
	}
	return d.Write(p1, p2)
}

func FetchMemInfo() (*client.Point, error) {
	infos := parseMemoryStatKB("/proc/meminfo", MemInfoKeys...)

	return client.NewPoint("meminfo", nil, toPoint(infos), time.Now())
}

func FetchCPUUsage() (*client.Point, error) {
	f, err := os.Open("/proc/stat")
	if err != nil {
		return nil, err
	}
	defer f.Close()
	var user, nice, system, idle, iowait int
	var cpu string
	fmt.Fscanln(f, &cpu, &user, &nice, &system, &idle, &iowait)
	infos := map[string]interface{}{
		"user":   user,
		"system": system,
		"idle":   idle,
		"iowait": iowait,
	}
	return client.NewPoint("cpu", nil, infos, time.Now())
}

func toPoint(d map[string]uint64) map[string]interface{} {
	ret := make(map[string]interface{})
	for k, v := range d {
		ret[k] = v
	}
	return ret
}

func toLines(v []byte, hasErr error) []string {
	if hasErr != nil {
		return nil
	}
	var ret []string
	for _, line := range strings.Split(string(v), "\n") {
		if line != "" {
			ret = append(ret, line)
		}
	}
	return ret
}

// ParseMemoryStatKB parse fields with KB suffix in /proc/self/status, /proc/meminfo
func parseMemoryStatKB(filePath string, keys ...string) map[string]uint64 {
	ret := make(map[string]uint64)
	for _, line := range toLines(ioutil.ReadFile(filePath)) {
		fields := strings.Split(line, ":")
		if len(fields) != 2 {
			continue
		}
		key := strings.TrimSpace(fields[0])
		for _, ikey := range keys {
			if key == ikey {
				value := strings.TrimSpace(fields[1])
				value = strings.Replace(value, " kB", "", -1)
				v, _ := strconv.ParseUint(value, 10, 64)
				ret[key] = v * KB
				if len(ret) >= len(keys) {
					return ret
				}
			}
		}
	}
	return ret
}
