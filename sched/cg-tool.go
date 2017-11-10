package main

import (
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"os/user"
	"path"
	"strconv"
	"strings"
	"syscall"
)

const (
	memoryCtrl  = "memory"
	cpuCtrl     = "cpu"
	freezerCtrl = "freezer"
)

func JoinCGPath(args ...string) string {
	return path.Join(SystemCGroupRoot, path.Join(args...))
}

var UserName = func() string {
	u, err := user.Current()
	if err != nil {
		panic("Can't find current username")
	}
	return u.Username
}()

func pathExist(p string) bool {
	_, err := os.Stat(p)
	return err == nil
}

func CheckPrepared(rootCGroup string) error {
	if !pathExist("/usr/bin/cgcreate") {
		return fmt.Errorf("先执行%q", "sudo apt-get install cgroup-tools")
	}
	groups := []string{
		JoinCGPath(memoryCtrl, rootCGroup),
		JoinCGPath(cpuCtrl, rootCGroup),
		JoinCGPath(freezerCtrl, rootCGroup),
	}
	for _, g := range groups {
		if !pathExist(g) {
			p := UserName + ":" + UserName
			cmd := fmt.Sprintf("sudo cgcreate -t %s -a %s -g memory,cpu,freezer:%s", p, p, rootCGroup)
			return fmt.Errorf("Please execute %q before running the sched program", cmd)
		}
	}
	return nil
}

func freeze(pids []int) {
	for _, p := range pids {
		syscall.Kill(p, syscall.SIGSTOP)
	}
}
func unFreeze(pids []int) {
	for _, p := range pids {
		syscall.Kill(p, syscall.SIGCONT)
	}
}

func _Command(c string, args ...string) *exec.Cmd {
	cmd := exec.Command(c, args...)
	fmt.Println("Run:", cmd.Args)
	return cmd
}

func CGCreate(ctrl string, path string) error {
	return os.MkdirAll(JoinCGPath(ctrl, path), 0700)
}
func CGDelete(ctrl string, path string) error {
	return os.Remove(JoinCGPath(ctrl, path))
}
func CGExec(ctrl string, path string, cmd string) error {
	// TODO remove cgexec by fork and exec
	return _Command("cgexec", "-g", ctrl+":"+path, cmd).Run()
}

// SystemMemoryInfo 返回 系统可用内存, 系统已用Swap
func SystemMemoryInfo() (uint64, uint64) {
	var available, swtotal, swfree uint64
	for _, line := range ToLines(ioutil.ReadFile("/proc/meminfo")) {
		fields := strings.Split(line, ":")
		if len(fields) != 2 {
			continue
		}
		key := strings.TrimSpace(fields[0])
		value := strings.TrimSpace(fields[1])
		value = strings.Replace(value, " kB", "", -1)
		t, err := strconv.ParseUint(value, 10, 64)
		if err != nil {
			return 0, 0
		}
		switch key {
		case "MemAvailable":
			available = t * 1024
		case "SwapTotal":
			swtotal = t * 1024
		case "SwapFree":
			swfree = t * 1024
		}
	}
	return available, swtotal - swfree
}

func ToUint64(v []byte, hasErr error) uint64 {
	if hasErr != nil {
		return 0
	}
	ret, _ := strconv.ParseUint(strings.TrimSpace(string(v)), 10, 64)
	return ret
}

func ToLines(v []byte, hasErr error) []string {
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

func ReadCGroupFile(ctrl string, name string, key string) ([]byte, error) {
	return ioutil.ReadFile(JoinCGPath(ctrl, name, key))
}

func FreezeUIApps(cgroup string) error {
	return WriteCGroupFile(freezerCtrl, cgroup, "freezer.state", "FROZEN")
}
func THAWEDUIApps(cgroup string) error {
	return WriteCGroupFile(freezerCtrl, cgroup, "freezer.state", "THAWED")
}
func WriteCGroupFile(ctrl string, name string, key string, value interface{}) error {
	fpath := JoinCGPath(ctrl, name, key)
	return ioutil.WriteFile(fpath, []byte(fmt.Sprintf("%v", value)), 0777)
}

func CGroupPIDs(ctrl string, name string) []int {
	var pids []int
	for _, line := range ToLines(ReadCGroupFile(ctrl, name, "cgroup.procs")) {
		pid, _ := strconv.ParseInt(line, 10, 32)
		if pid != 0 {
			pids = append(pids, int(pid))
		}
	}
	return pids
}

func SetLimitRSS(cgroup string, v uint64) error {
	return WriteCGroupFile(memoryCtrl, cgroup, "memory.soft_limit_in_bytes", v)
}
