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

const memoryCtrl = "memory"
const baseCGroup = "/sys/fs/cgroup"
const baseCGDir = "77@dde/uiapps"

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

func CheckPrepared() error {
	groups := []string{
		path.Join(baseCGroup, memoryCtrl, baseCGDir),
		path.Join(baseCGroup, memoryCtrl, baseCGDir),
		path.Join(baseCGroup, memoryCtrl, baseCGDir),
	}
	for _, g := range groups {
		if !pathExist(g) {
			p := UserName + ":" + UserName
			return fmt.Errorf("Please execute %q before running the sched program",
				fmt.Sprintf("sudo cgreate -t %s -a %s -g memory,cpu,freezer:%s", p, p, baseCGDir))
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
	contents, err := ioutil.ReadFile("/proc/meminfo")
	if err != nil {
		return 0, 0
	}

	var available, swtotal, swfree uint64
	for _, line := range strings.Split(string(contents), "\n") {
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

func CGroupPIDs(ctrl string, name string) []int {
	var pids []int
	bs, err := ioutil.ReadFile(path.Join(baseCGroup, ctrl, name, "cgroup.procs"))
	if err != nil {
		return pids
	}
	for _, line := range strings.Split(string(bs), "\n") {
		pid, _ := strconv.ParseInt(line, 10, 32)
		if pid != 0 {
			pids = append(pids, int(pid))
		}
	}
	return pids
}

func SetLimitRSS(cgroup string, v uint64) error {
	// TODO: send SIGSTOP to process in cgroup.procs for avoiding write failed
	fpath := path.Join(baseCGroup, memoryCtrl, cgroup, "memory.limit_in_bytes")

	pids := CGroupPIDs(memoryCtrl, cgroup)
	freeze(pids)
	err := ioutil.WriteFile(fpath, []byte(fmt.Sprintf("%v", v)), 0777)
	unFreeze(pids)
	return err
}
