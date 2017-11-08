package main

import (
	"fmt"
	"io/ioutil"
	"path"
	"strconv"
	"strings"
)

const memoryCtrl = "memory"
const baseCGroup = "/sys/fs/cgroup"
const baseCGDir = "77@dde/uiapps"

type UIApp struct {
	cgroup string
	CMD    string
	limit  uint64
	live   bool
}

func (app *UIApp) HasChild(pid int) bool {
	bs, err := ioutil.ReadFile(path.Join(baseCGroup, memoryCtrl, app.cgroup, "cgroup.procs"))
	if err != nil {
		return false
	}
	pidStr := fmt.Sprintf("%d", pid)
	for _, line := range strings.Split(string(bs), "\n") {
		if pidStr == line {
			return true
		}
	}
	return false
}

func (app *UIApp) RSS() uint64 {
	bs, err := ioutil.ReadFile(path.Join(baseCGroup, memoryCtrl, app.cgroup, "memory.stat"))
	if err != nil {
		return 0
	}
	for _, line := range strings.Split(string(bs), "\n") {
		const t = "total_active_anon "
		if strings.HasPrefix(line, t) {
			v, _ := strconv.ParseUint(line[len(t):], 10, 64)
			return v
		}
	}
	return 0
}

func (app *UIApp) PIDs() []int {
	return CGroupPIDs(memoryCtrl, app.cgroup)
}

func (app *UIApp) SetLimitRSS(v uint64) error {
	app.limit = v

	return SetLimitRSS(app.cgroup, v)
}

func (app *UIApp) LimitRSS() uint64 {
	return app.limit
}

func (app *UIApp) Run() error {
	defer func() {
		CGDelete(memoryCtrl, app.cgroup)
		app.live = false
	}()

	app.live = true
	return CGExec(memoryCtrl, app.cgroup, app.CMD)
}

func NewApp(id int, cmd string) (*UIApp, error) {
	cgroup := fmt.Sprintf("%s/%d", baseCGDir, id)
	err := CGCreate(memoryCtrl, cgroup)
	if err != nil {
		return nil, err
	}
	return &UIApp{cgroup, cmd, 0, false}, nil
}
