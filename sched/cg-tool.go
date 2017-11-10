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
	baseCGDir   = "77@dde/uiapps"
	rootCGroup  = "/sys/fs/cgroup"
)

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

func CheckPrepared(auto bool) error {
	if !pathExist("/usr/bin/cgcreate") {
		return fmt.Errorf("先执行%q", "sudo apt-get install cgroup-tools")
	}
	groups := []string{
		path.Join(rootCGroup, memoryCtrl, baseCGDir),
		path.Join(rootCGroup, cpuCtrl, baseCGDir),
		path.Join(rootCGroup, freezerCtrl, baseCGDir),
	}
	for _, g := range groups {
		if !pathExist(g) {
			p := UserName + ":" + UserName
			cmd := fmt.Sprintf("sudo cgcreate -t %s -a %s -g memory,cpu,freezer:%s", p, p, baseCGDir)
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
	p := UserName + ":" + UserName
	return _Command("cgcreate",
		"-t", p,
		"-a", p,
		"-g", ctrl+":"+path,
	).Run()
}

func CGDelete(ctrl string, path string) error {
	return _Command("cgdelete", "-r", "-g", ctrl+":"+path).Run()
}

func CGExec(ctrl string, path string, cmd string) error {
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
	return ioutil.ReadFile(path.Join(rootCGroup, ctrl, name, key))
}

func FreezeUIApps() error {
	return WriteCGroupFile(freezerCtrl, baseCGDir, "freezer.state", "FROZEN")
}
func THAWEDUIApps() error {
	return WriteCGroupFile(freezerCtrl, baseCGDir, "freezer.state", "THAWED")
}

func WriteCGroupFile(ctrl string, name string, key string, value interface{}) error {
	fpath := path.Join(rootCGroup, ctrl, name, key)
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
	return WriteCGroupFile(memoryCtrl, cgroup, "memory.limit_in_bytes", v)
}
