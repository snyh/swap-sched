package main

import (
	"fmt"
	"sync"
	"time"
)

type TuneConfig struct {
	RootCGroup            string        // 默认的root cgroup path , 需要外部程序提前配置好为uid可以操控的,且需要有cpu,memory,freezer3个control
	MemoryLock            bool          // 是否调用MLockAll
	FreezeInactiveAppTime time.Duration // 在freeze所有UI APP到设置inacitve apps的时间间隔
}

type Dispatcher struct {
	sync.Mutex

	cfg     TuneConfig
	counter int

	activeXID int

	activeApp    *UIApp
	inactiveApps []*UIApp
}

func NewDispatcher(cfg TuneConfig) *Dispatcher {
	return &Dispatcher{
		cfg:       cfg,
		counter:   0,
		activeXID: -1,
	}
}

func (d *Dispatcher) Counter() int {
	d.Lock()
	d.counter = d.counter + 1
	d.Unlock()
	return d.counter
}

func (d *Dispatcher) Run(cmd string) error {
	cgroup := fmt.Sprintf("%s/%d", d.cfg.RootCGroup, d.Counter())
	app, err := NewApp(cgroup, cmd)
	if err != nil {
		return err
	}
	d.addApps(app)
	return app.Run()
}

func (d *Dispatcher) addApps(app *UIApp) {
	d.Lock()
	d.inactiveApps = append(d.inactiveApps, app)
	d.Unlock()
}

func (d *Dispatcher) ActiveWindowHanlder(pid int, xid int) {
	if d.activeXID == xid {
		return
	}
	d.activeXID = xid

	if pid == 0 {
		d.setActiveApp(nil)
		return
	}

	d.Lock()
	if d.activeApp != nil && d.activeApp.HasChild(pid) {
		d.Unlock()
		return
	}

	var newActive *UIApp
	for _, app := range d.inactiveApps {
		if app.HasChild(pid) {
			newActive = app
			break
		}
	}
	d.Unlock()
	d.setActiveApp(newActive)
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

	if d.activeApp == nil {
		fmt.Printf("--------DEBUG--NO Active APP (_NET_ACTIVE_WINDOW: %d)----------\n%s\n\n", d.activeXID, info)
	} else {
		fmt.Printf("--------DEBUG--Active APP: %q(%q) (%dMB,%dMB)----------\n%s\n\n",
			d.activeApp.CMD,
			d.activeApp.cgroup,
			info.ActiveAppRSS/MB, info.ActiveAppSwap/MB,
			info)
	}

	FreezeUIApps(d.cfg.RootCGroup)
	defer THAWEDUIApps(d.cfg.RootCGroup)

	err := SetLimitRSS(d.cfg.RootCGroup, info.UIAppsLimit())
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
	time.Sleep(d.cfg.FreezeInactiveAppTime)

	var liveApps []*UIApp
	for _, app := range d.inactiveApps {
		if !app.IsLive() {
			continue
		}
		err = app.SetLimitRSS(ilimit)
		if err != nil {
			fmt.Println("SetActtiveAppLimit failed:", app, err)
		}
		liveApps = append(liveApps, app)
	}
	d.inactiveApps = liveApps
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

	n int
}

func (info MemInfo) UIAppsLimit() uint64 {
	return info.TotalRSSFree + info.ActiveAppRSS + info.InactiveAppsRSS
}

func (info MemInfo) String() string {
	str := fmt.Sprintf("TotalFree %dMB, SwapUsed: %dMB\n",
		info.TotalRSSFree/MB, info.TotalUsedSwap/MB)
	str += fmt.Sprintf("UI Limit: %dMB\nActive App Limit: %dMB (need %dMB)\nInAcitve App Limit %dMB (%d need %dMB)",
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
	info.n = len(d.inactiveApps)

	for _, app := range d.inactiveApps {
		rss, swap := app.MemoryInfo()
		info.InactiveAppsRSS += rss
		info.InactiveAppsSwap += swap
	}

	if d.activeApp != nil {
		rss, swap := d.activeApp.MemoryInfo()
		info.ActiveAppRSS = rss
		info.ActiveAppSwap = swap
	}
	return info
}
