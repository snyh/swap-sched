package main

import (
	"fmt"
	"strconv"
	"strings"
	"time"
)

type UIApp struct {
	cgroup string
	CMD    string
	limit  uint64
	live   bool
}

func (app *UIApp) HasChild(pid int) bool {
	pidStr := fmt.Sprintf("%d", pid)
	for _, line := range ToLines(ReadCGroupFile(memoryCtrl, app.cgroup, "cgroup.procs")) {
		if pidStr == line {
			return true
		}
	}
	return false
}

// MemoryInfo 返回 RSS 以及 Swap使用量 (目前数据不对)
func (app *UIApp) MemoryInfo() (uint64, uint64) {
	if !app.live {
		return 0, 0
	}

	used := ToUint64(ReadCGroupFile(memoryCtrl, app.cgroup, "memory.usage_in_bytes"))
	for _, line := range ToLines(ReadCGroupFile(memoryCtrl, app.cgroup, "memory.stat")) {
		const ta = "total_active_anon "
		if strings.HasPrefix(line, ta) {
			v, _ := strconv.ParseUint(line[len(ta):], 10, 64)
			if v > used {
				break
			}
			return used - v, v
		}
	}
	return used, 0
}

func (app *UIApp) PIDs() []int {
	if !app.live {
		return nil
	}

	pids := CGroupPIDs(memoryCtrl, app.cgroup)
	if len(pids) == 0 {
		app.live = false
	}
	return pids
}

func (app *UIApp) SetLimitRSS(v uint64) error {
	if !app.live {
		return nil
	}
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
		for {
			time.Sleep(time.Millisecond * 100)
			if app.live == false {
				CGDelete(memoryCtrl, app.cgroup)
				break
			}
		}
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
