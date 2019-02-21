package main

import (
	"bufio"
	"bytes"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"text/tabwriter"
	"time"

	"github.com/c2h5oh/datasize"
	"github.com/wsxiaoys/terminal"
	"pkg.deepin.io/lib/procfs"
)

const cGroupRoot = "/sys/fs/cgroup"

var sessionID string
var interval float64
var colorFlag bool

func init() {
	flag.StringVar(&sessionID, "sessionid", "", "session id")
	flag.Float64Var(&interval, "interval", 0, "update interval")
	flag.BoolVar(&colorFlag, "color", true, "terminal color output")
	log.SetOutput(os.Stdout)
	log.SetFlags(log.Lshortfile)
}

func main() {
	flag.Parse()

	var out io.Writer
	var buf bytes.Buffer
	if colorFlag {
		out = &buf
	} else {
		out = os.Stdout
	}

	tabW := tabwriter.NewWriter(out, 0, 0, 1, ' ', tabwriter.Debug)

	if sessionID == "" {
		var err error
		sessionID, err = getSessionID()
		if err != nil {
			log.Fatal(err)
		}
	}

	duration := time.Duration(float64(time.Second) * interval)

	if duration > 0 {
		for {
			clearScreen()
			display(tabW, &buf)
			time.Sleep(duration)
		}
	} else {
		display(tabW, &buf)
	}
}

func clearScreen() {
	os.Stdout.Write([]byte("\x1b[H\x1b[J"))
}

func display(tabW *tabwriter.Writer, buf *bytes.Buffer) {
	writeTable(tabW)
	if colorFlag {
		colorOutput(buf)
	}
	memInfo, err := getMemoryInfo()
	if err == nil {
		fmt.Printf("memory total: %s, free: %s, available: %s swap usage: %s\n",
			memInfo.total.HR(), memInfo.free.HR(), memInfo.available.HR(), memInfo.swapusage.HR(),
		)
	}
}

func colorOutput(out *bytes.Buffer) {
	termW := terminal.Stdout
	rd := bufio.NewReader(out)

	count := 0
	for {
		line, err := rd.ReadBytes('\n')
		if err != nil {
			if err == io.EOF {
				break
			}
			log.Fatal(err)
		}

		if count == 0 { // first line
			termW.Write(line)
		} else {
			var color string
			switch (count - 1) % 6 {
			case 0:
				color = "r"
			case 1:
				color = "g"
			case 2:
				color = "b"
			case 3:
				color = "c"
			case 4:
				color = "m"
			case 5:
				color = "y"
			}
			termW.Colorf("@"+color+"%s@{|}", line)
		}
		count++
	}

	out.Reset()
}

func writeTable(w *tabwriter.Writer) {
	// print header
	fmt.Fprintln(w, "group\tlimit Hard, Soft\tUsage RSS, Cache, Swap\tpgpgin, pgpgout, MajFault\t")

	deCGroup := getDECGroup(sessionID)
	r, err := getRecord(deCGroup)
	if err == nil {
		r.writeLine(w, "DE")
	}

	uiappsCGroup := getUIAppsCGroup(sessionID)
	fileInfoList, err := ioutil.ReadDir(uiappsCGroup)
	if err != nil {
		log.Fatal(err)
	}

	var uiappsR Record

	for _, fileInfo := range fileInfoList {
		if !fileInfo.IsDir() {
			continue
		}

		_, err := strconv.Atoi(fileInfo.Name())
		if err != nil {
			continue
		}

		uiappCGroup := filepath.Join(uiappsCGroup, fileInfo.Name())

		r, err := getRecord(uiappCGroup)
		if err != nil {
			log.Fatal(err)
		}

		uiappsR.rssUsage += r.rssUsage
		uiappsR.swapUsage += r.swapUsage
		uiappsR.cacheUsage += r.cacheUsage

		uiappsR.pageIn += r.pageOut
		uiappsR.pageOut += r.pageOut
		uiappsR.majorFault += r.majorFault

		name := fileInfo.Name()
		if r.firstProcName != "" {
			name = fmt.Sprintf("%s %s", name, r.firstProcName)
		}

		r.writeLine(w, name)
	}

	uiappsR.writeLine(w, "uiapps")
	w.Flush()
}

type Record struct {
	firstProcName string

	// limit
	limit     datasize.ByteSize // hard limit
	softLimit datasize.ByteSize

	// usage
	rssUsage   datasize.ByteSize
	swapUsage  datasize.ByteSize
	cacheUsage datasize.ByteSize

	// fail
	pageIn     uint64
	pageOut    uint64
	majorFault uint64
}

type MemoryStat struct {
	rss         uint64
	cache       uint64
	mappedFiles uint64

	swap uint64

	pageIn     uint64
	pageOut    uint64
	majorFault uint64
}

type MemoryInfo struct {
	total     datasize.ByteSize
	free      datasize.ByteSize
	available datasize.ByteSize
	swapusage datasize.ByteSize
}

func getMemoryInfo() (*MemoryInfo, error) {
	f, err := os.Open("/proc/meminfo")
	if err != nil {
		return nil, err
	}
	defer f.Close()
	scanner := bufio.NewScanner(f)
	var memInfo MemoryInfo
	const countMax = 5
	count := 0
	for scanner.Scan() {
		fields := bytes.Fields(scanner.Bytes())
		if len(fields) != 3 {
			continue
		}
		val, _ := strconv.ParseUint(string(fields[1]), 10, 64)
		switch string(fields[0]) {
		case "MemTotal:":
			memInfo.total = datasize.ByteSize(val) * datasize.KB
			count++
		case "MemFree:":
			memInfo.free = datasize.ByteSize(val) * datasize.KB
			count++
		case "MemAvailable:":
			memInfo.available = datasize.ByteSize(val) * datasize.KB
			count++
		case "SwapTotal:":
			memInfo.swapusage = datasize.ByteSize(val)
			count++
		case "SwapFree:":
			memInfo.swapusage = (memInfo.swapusage - datasize.ByteSize(val)) * datasize.KB
			count++
		}

		if count == countMax {
			break
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return &memInfo, nil
}

func getMemoryStat(path string) (*MemoryStat, error) {
	f, err := os.Open(filepath.Join(path, "memory.stat"))
	if err != nil {
		return nil, err
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	var ms MemoryStat
	const countMax = 7
	count := 0
	for scanner.Scan() {
		fields := bytes.Fields(scanner.Bytes())
		if len(fields) != 2 {
			continue
		}

		val, _ := strconv.ParseUint(string(fields[1]), 10, 64)

		switch string(fields[0]) {
		case "rss":
			ms.rss = val
			count++
		case "cache":
			ms.cache = val
			count++
		case "mapped_files":
			ms.mappedFiles = val
			count++
		case "swap":
			ms.swap = val
			count++
		case "pgpgin":
			ms.pageIn = val
			count++
		case "pgpgout":
			ms.pageOut = val
			count++
		case "pgmajfault":
			ms.majorFault = val
			count++
		}

		if count == countMax {
			break
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return &ms, nil
}

func getRecord(path string) (*Record, error) {
	limit, err := getNum(filepath.Join(path, "memory.limit_in_bytes"))
	if err != nil {
		return nil, err
	}

	softLimit, err := getNum(filepath.Join(path, "memory.soft_limit_in_bytes"))
	if err != nil {
		return nil, err
	}

	memStat, err := getMemoryStat(path)
	if err != nil {
		return nil, err
	}

	return &Record{
		firstProcName: getFirstProcNameCache(path),
		limit:         datasize.ByteSize(limit),
		softLimit:     datasize.ByteSize(softLimit),

		rssUsage:   datasize.ByteSize(memStat.rss + memStat.mappedFiles),
		swapUsage:  datasize.ByteSize(memStat.swap),
		cacheUsage: datasize.ByteSize(memStat.cache),

		pageIn:     memStat.pageIn,
		pageOut:    memStat.pageOut,
		majorFault: memStat.majorFault,
	}, nil
}

var firstProcNameCache = make(map[string]string)

func getFirstProcNameCache(path string) string {
	base := filepath.Base(path)
	val, ok := firstProcNameCache[base]
	if ok {
		return val
	}
	ret := getFirstProcName(path)

	const nameLimit = 25
	if len(ret) > nameLimit {
		ret = ret[:nameLimit-1] + "~"
	}
	firstProcNameCache[base] = ret
	return ret
}

func getFirstProcName(path string) string {
	var firstProcName string
	procs, err := getNums(filepath.Join(path, "cgroup.procs"))
	if err != nil {
		return ""
	}

	if len(procs) > 0 {
		p := procfs.Process(procs[0])
		envVars, _ := p.Environ()
		desktopFile := envVars.Get("GIO_LAUNCHED_DESKTOP_FILE")
		if desktopFile != "" {
			firstProcName = strings.TrimSuffix(filepath.Base(desktopFile), ".desktop")
		} else {
			// try cmdline
			cmdline, _ := p.Cmdline()

			if len(cmdline) > 0 {
				firstProcName = "bin:" + filepath.Base(cmdline[0])
			}
		}
	}
	return firstProcName
}

func getCountHR(val uint64) string {
	return strings.TrimSuffix(datasize.ByteSize(val).HR(), "B")
}

func (r *Record) writeLine(w io.Writer, name string) {
	delta := (r.rssUsage + r.cacheUsage + r.swapUsage)
	const format = "%s\t%8s, %8s(%2s)\t%8s, %8s, %8s\t%7s, %7s, %7s\t\n"
	fmt.Fprintf(w, format, name,
		r.limit.HR(), r.softLimit.HR(), delta.HR(),
		r.rssUsage.HR(), r.cacheUsage.HR(), r.swapUsage.HR(),
		getCountHR(r.pageIn), getCountHR(r.pageOut), getCountHR(r.majorFault),
	)
}

func getSessionID() (string, error) {
	content, err := ioutil.ReadFile("/proc/self/sessionid")
	if err != nil {
		return "", err
	}
	return string(content), err
}

func getDECGroup(sessionID string) string {
	return filepath.Join(cGroupRoot, "memory", sessionID+"@dde", "DE")
}

func getUIAppsCGroup(sessionID string) string {
	return filepath.Join(cGroupRoot, "memory", sessionID+"@dde", "uiapps")
}

func getNums(filename string) ([]int, error) {
	content, err := ioutil.ReadFile(filename)
	if err != nil {
		return nil, err
	}
	fields := bytes.Fields(content)
	var ret []int
	for _, field := range fields {
		num, err := strconv.Atoi(string(field))
		if err != nil {
			return nil, err
		}
		ret = append(ret, num)
	}
	return ret, nil
}

func getNum(filename string) (int, error) {
	content, err := ioutil.ReadFile(filename)
	if err != nil {
		return 0, err
	}
	content = bytes.TrimSpace(content)
	num, err := strconv.Atoi(string(content))
	if err != nil {
		return 0, err
	}
	return num, nil
}
