package main

import (
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
	pids := CGroupPIDs(memoryCtrl, app.cgroup)
	if len(pids) == 0 {
		app.live = false
		return false
	}
	for _, pid_ := range pids {
		if pid_ == pid {
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

func NewApp(subCGroup string, cmd string) (*UIApp, error) {
	err := CGCreate(memoryCtrl, subCGroup)
	if err != nil {
		return nil, err
	}
	return &UIApp{subCGroup, cmd, 0, false}, nil
}
