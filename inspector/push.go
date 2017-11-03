package main

import (
	"fmt"
	"github.com/influxdata/influxdb/client/v2"
	"time"
)

type Client struct {
	c      client.Client
	dbname string
}

func (c *Client) Push(infos []ProcessInfo) error {
	bp, err := client.NewBatchPoints(client.BatchPointsConfig{
		Database:  c.dbname,
		Precision: "ms",
	})
	if err != nil {
		return err
	}

	for _, info := range infos {
		tags := map[string]string{
			"pid":  fmt.Sprintf("%d", info["Pid"]),
			"name": info["Name"].(string),
		}
		pt, err := client.NewPoint("process", tags, info, time.Now())
		if err != nil {
			return fmt.Errorf("NewPoint: %v", err)
		}
		bp.AddPoint(pt)
	}
	return c.c.Write(bp)
}

func (c *Client) Close() error { return c.c.Close() }

func NewClient(addr string, user string, passwd string, dbname string) (*Client, error) {
	c, err := client.NewHTTPClient(client.HTTPConfig{
		Addr:     addr,
		Username: user,
		Password: passwd,
	})
	if err != nil {
		return nil, err
	}
	_, _, err = c.Ping(time.Second)
	if err != nil {
		return nil, err
	}
	c.Query(client.Query{
		Command: fmt.Sprintf("drop database %s", dbname),
	})
	c.Query(client.Query{
		Command: fmt.Sprintf("create database %s", dbname),
	})
	return &Client{c, dbname}, nil
}
