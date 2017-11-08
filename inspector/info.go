package main

import (
	"fmt"
	"io/ioutil"
	"os"
	"strconv"
	"strings"
)

func Pids() ([]int32, error) {
	var ret []int32
	d, err := os.Open("/proc")
	if err != nil {
		return nil, err
	}
	defer d.Close()

	fnames, err := d.Readdirnames(-1)
	if err != nil {
		return nil, err
	}
	for _, fname := range fnames {
		pid, err := strconv.ParseInt(fname, 10, 32)
		if err != nil {
			continue
		}
		ret = append(ret, int32(pid))
	}
	return ret, nil
}

type ProcessInfo map[string]interface{}

// To Convert it to a map[string]interface{},
// for simply write data to influxdb
func (p _ProcessInfo) To() ProcessInfo {
	return map[string]interface{}{
		"Pid":         p.Pid,
		"Name":        p.Name,
		"Status":      p.Status,
		"Voluntary":   p.Voluntary,
		"InVoluntary": p.InVoluntary,
		"MinFlt":      int64(p.MinFlt),
		"MajFlt":      int64(p.MajFlt),
		"VMS":         int64(p.VMS),
		"RSS":         p.RSS,
		"Swap":        int64(p.Swap),
		"ReadBytes":   int64(p.ReadBytes),
		"WriteBytes":  int64(p.WriteBytes),
		"STime":       p.STime,
		"UTime":       p.UTime,
	}
}

type _ProcessInfo struct {
	Pid  int32
	Name string

	Status      string
	Voluntary   int64
	InVoluntary int64

	MinFlt uint64
	MajFlt uint64

	VMS  uint64
	RSS  int64
	Swap uint64

	ReadBytes  uint64
	WriteBytes uint64

	STime int64 // unit in CLockTicks
	UTime int64 // unit in ClockTicks
}

var NullInfo ProcessInfo

func FetchProcessInfo(pid int32) (ProcessInfo, error) {
	basePath := fmt.Sprintf("/proc/%d/", pid)
	var p _ProcessInfo
	p.Pid = pid

	{
		// parse /proc/$pid/io
		contents, err := ioutil.ReadFile(basePath + "io")
		if err != nil {
			return NullInfo, err
		}
		for _, line := range strings.Split(string(contents), "\n") {
			field := strings.Split(line, ":")
			if len(field) < 2 {
				continue
			}
			switch field[0] {
			case "read_bytes":
				t, err := strconv.ParseUint(strings.TrimSpace(field[1]), 10, 64)
				if err != nil {
					return NullInfo, err
				}
				p.ReadBytes = t
			case "write_bytes":
				t, err := strconv.ParseUint(strings.TrimSpace(field[1]), 10, 64)
				if err != nil {
					return NullInfo, err
				}
				p.WriteBytes = t
			default:
				continue
			}
		}
	}

	{
		// parse /proc/$pid/stat
		contents, err := ioutil.ReadFile(basePath + "stat")
		if err != nil {
			return NullInfo, err
		}
		fields := strings.Fields(string(contents))
		i := 1
		// skip command pair "()"
		for !strings.HasSuffix(fields[i], ")") {
			i++
		}

		p.MinFlt, err = strconv.ParseUint(fields[i-2+10], 10, 64)
		if err != nil {
			return NullInfo, err
		}
		p.MajFlt, err = strconv.ParseUint(fields[i-2+12], 10, 64)
		if err != nil {
			return NullInfo, err
		}

		p.UTime, err = strconv.ParseInt(fields[i-2+14], 10, 64)
		if err != nil {
			return NullInfo, err
		}
		p.STime, err = strconv.ParseInt(fields[i-2+15], 10, 64)
		if err != nil {
			return NullInfo, err
		}
		p.Status = fields[i-2+3]

		p.VMS, err = strconv.ParseUint(fields[i-2+23], 10, 64)
		if err != nil {
			return NullInfo, err
		}

	}

	{
		// parse /proc/$pid/status
		contents, err := ioutil.ReadFile(basePath + "status")
		if err != nil {
			return NullInfo, err
		}
		lines := strings.Split(string(contents), "\n")

		for _, line := range lines {
			tabParts := strings.SplitN(line, "\t", 2)
			if len(tabParts) < 2 {
				continue
			}
			value := tabParts[1]
			switch strings.TrimRight(tabParts[0], ":") {
			case "Name":
				p.Name = strings.Trim(value, " \t")
			case "voluntary_ctxt_switches":
				v, err := strconv.ParseInt(value, 10, 64)
				if err != nil {
					return NullInfo, err
				}
				p.Voluntary = v
			case "nonvoluntary_ctxt_switches":
				v, err := strconv.ParseInt(value, 10, 64)
				if err != nil {
					return NullInfo, err
				}
				p.InVoluntary = v
			case "VmSwap":
				value := strings.Trim(value, " kB") // remove last "kB"
				v, err := strconv.ParseUint(value, 10, 64)
				if err != nil {
					return NullInfo, err
				}
				p.Swap = v * 1024
			case "VmRSS":
				value := strings.Trim(value, " kB") // remove last "kB"
				v, err := strconv.ParseInt(value, 10, 64)
				if err != nil {
					return NullInfo, err
				}
				p.RSS = v * 1024
			}
		}
	}
	return p.To(), nil
}
