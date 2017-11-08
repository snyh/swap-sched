package main

import (
	"fmt"
	"sync"
	"time"
)

type Dispatcher struct {
	sync.Mutex

	counter int

	activeApp    *UIApp
	inactiveApps []*UIApp
}

func NewDispatcher() *Dispatcher {
	return &Dispatcher{
		counter: 0,
	}
}

func (d *Dispatcher) Counter() int {
	d.Lock()
	d.counter = d.counter + 1
	d.Unlock()
	return d.counter
}

func (d *Dispatcher) Run(cmd string) error {
	app, err := NewApp(d.Counter(), cmd)
	if err != nil {
		return err
	}
	d.addApps(app)
	return app.Run()
}

func (d *Dispatcher) ActiveWindowHanlder(pid int) {
	var newActive *UIApp
	d.Lock()
	defer func() {
		d.Unlock()
		d.setActiveApp(newActive)
	}()

	if d.activeApp != nil && d.activeApp.HasChild(pid) {
		return
	}
	for _, app := range d.inactiveApps {
		if app.HasChild(pid) {
			newActive = app
			break
		}
	}
}

func (d *Dispatcher) addApps(app *UIApp) {
	d.Lock()
	d.inactiveApps = append(d.inactiveApps, app)
	d.Unlock()
}

func (d *Dispatcher) setActiveApp(newActive *UIApp) {
	if d.activeApp == newActive {
		return
	}

	d.Lock()
	var newInactiveApps []*UIApp
	if d.activeApp != nil {
		newInactiveApps = append(newInactiveApps, d.activeApp)
	}
	for _, i := range d.inactiveApps {
		if i == newActive {
			continue
		}
		newInactiveApps = append(newInactiveApps, i)
	}

	d.inactiveApps = newInactiveApps
	d.activeApp = newActive
	d.Unlock()

	d.blance()
}

func (d *Dispatcher) blance() {
	d.Lock()
	defer d.Unlock()

	info := d.Sample()
	fmt.Printf("--------DEBUG------------\n%s\n\n", info)

	err := SetLimitRSS(baseCGDir, info.UIAppsLimit())
	if err != nil {
		fmt.Println("SetUIAppsLimit failed:", err)
	}

	if d.activeApp != nil {
		err = d.activeApp.SetLimitRSS(info.ActiveAppLimit())
		if err != nil {
			fmt.Println("SetActtiveAppLimit failed:", d.activeApp, err)
		}
	}

	ilimit := info.InactiveAppLimit()
	for _, app := range d.inactiveApps {
		err = app.SetLimitRSS(ilimit)
		if err != nil {
			fmt.Println("SetActtiveAppLimit failed:", app, err)
		}
	}
}

func (d *Dispatcher) Blance() {
	for {
		time.Sleep(time.Second)
		d.blance()
	}
}

type MemInfo struct {
	TotalRSSFree     uint64 //当前一共可用的物理内存
	TotalUsedSwap    uint64 //当前已使用的Swap内存
	ActiveAppRSS     uint64 //活跃App占用的物理内存
	ActiveAppSwap    uint64 //活跃App占用的Swap内存
	InactiveAppsRSS  uint64 //除活跃App外所有APP一共占用的物理内存 (不含DDE等非UI APP组件)
	InactiveAppsSwap uint64 //除活跃App外所有APP一共占用的Swap内存 (不含DDE等非UI APP组件)
	n                int
}

func (info MemInfo) UIAppsLimit() uint64 {
	return info.TotalRSSFree + info.ActiveAppRSS + info.InactiveAppsRSS
}

func (info MemInfo) String() string {
	str := fmt.Sprintf("TotalFree %dMB, SwapUsed: %dMB\n",
		info.TotalRSSFree/MB, info.TotalUsedSwap/MB)
	str += fmt.Sprintf("UI Limit: %dMB\nActive App Limit: %dMB (1 need %dMB)\nInAcitve App Limit %dMB (%d need %dMB)",
		info.UIAppsLimit()/MB,
		info.ActiveAppLimit()/MB,
		(info.ActiveAppRSS+info.ActiveAppSwap)/MB,
		info.InactiveAppLimit()/MB,
		info.n,
		(info.InactiveAppsRSS+info.InactiveAppsSwap)/MB,
	)
	return str
}

const MB = 1000 * 1000

func (info MemInfo) ActiveAppLimit() uint64 {
	if info.ActiveAppRSS == 0 {
		return 0
	}

	max := func(a, b uint64) uint64 {
		if a > b {
			return a
		}
		return b
	}

	// 逐步满足ActiveApp的内存需求，但上限由UIAppsLimit()决定(cgroup本身会保证,不需要在这里做截断)。
	return max(info.ActiveAppRSS+100*MB, info.UIAppsLimit()-100*MB)
}

func (info MemInfo) InactiveAppLimit() uint64 {
	// TODO: 是否需要除以app数量?
	//优先保证ActiveApp有机会完全加载到RSS中
	min := func(a, b uint64) uint64 {
		if a < b {
			return a
		}
		return b
	}
	activeMemory := info.ActiveAppRSS + info.ActiveAppSwap
	return min(info.UIAppsLimit()-activeMemory, info.TotalRSSFree-activeMemory)
}

func (d *Dispatcher) Sample() MemInfo {
	var info MemInfo
	info.TotalRSSFree, info.TotalUsedSwap = SystemMemoryInfo()

	for _, app := range d.inactiveApps {
		info.InactiveAppsRSS += app.RSS()
	}

	if d.activeApp != nil {
		info.ActiveAppRSS = d.activeApp.RSS()
	}
	info.n = len(d.inactiveApps)
	return info
}
