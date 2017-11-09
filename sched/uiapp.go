package main

import (
	"fmt"
	"io/ioutil"
	"path"
	"strconv"
	"strings"
)

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

func (app *UIApp) RSS() (uint64, uint64) {
	bs, err := ioutil.ReadFile(path.Join(baseCGroup, memoryCtrl, app.cgroup, "memory.stat"))
	if err != nil {
		return 0, 0
	}

	var aaSize, iaSize uint64
	for _, line := range strings.Split(string(bs), "\n") {
		const ta = "total_active_anon "
		const tia = "total_inactive_anon "
		switch {
		case strings.HasPrefix(line, ta):
			aaSize, _ = strconv.ParseUint(line[len(ta):], 10, 64)
		case strings.HasPrefix(line, tia):
			iaSize, _ = strconv.ParseUint(line[len(tia):], 10, 64)
		}
		if aaSize != 0 && iaSize != 0 {
			return aaSize, iaSize
		}
	}
	return 0, 0
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
func (app *UIApp) IsLive() bool {
	return app.live
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
