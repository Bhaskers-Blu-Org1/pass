// author john.d.sheehan@ie.ibm.com

package main

import (
	"encoding/json"
	"flag"
	"html/template"
	"io"
	"io/ioutil"
	"log"
	"net/http"
	"path/filepath"
	"strconv"
	"sync"

	"github.com/IBM/pass/pass/utils/view/pkg/relay"
	"github.com/gorilla/websocket"
)

const (
	socketBufferSize  = 1024
	messageBufferSize = 256
)

type message struct {
	Name    string    `json:"name"`
	Type    string    `json:"type"`
	Sensor  int       `json:"sensor"`
	Channel int       `json:"channel"`
	Values  []float32 `json:"values"`
}

var upgrader = &websocket.Upgrader{
	ReadBufferSize:  socketBufferSize,
	WriteBufferSize: socketBufferSize,
	CheckOrigin:     func(r *http.Request) bool { return true },
}

type client struct {
	socket  *websocket.Conn
	send    chan []byte
	msgType string
	sensor  int
	channel int
	mtx     sync.Mutex
}

func (c *client) read() {
	defer c.socket.Close()
	for {
		_, msg, err := c.socket.ReadMessage()
		if err != nil {
			log.Printf("socket read error: %s\n", err)
			return
		}

		type clientMsg struct {
			Type    string `json:"type"`
			Sensor  int    `json:"sensor"`
			Channel int    `json:"channel"`
		}

		var cm clientMsg
		err = json.Unmarshal(msg, &cm)
		if err != nil {
			log.Print(err)
			continue
		}

		log.Printf("------------------>request switch to %s, %d, %d", cm.Type, cm.Sensor, cm.Channel)

		c.mtx.Lock()
		c.msgType = cm.Type
		c.sensor = cm.Sensor
		c.channel = cm.Channel
		c.mtx.Unlock()

		log.Print("read: ", string(msg))
	}
}

func (c *client) write() {
	defer c.socket.Close()

	for msg := range c.send {
		c.mtx.Lock()
		msgType := c.msgType
		sensor := c.sensor
		channel := c.channel
		c.mtx.Unlock()

		type check struct {
			Type    string `json:"type"`
			Sensor  int    `json:"sensor"`
			Channel int    `json:"channel"`
		}

		var chk check
		err := json.Unmarshal(msg, &chk)
		if err != nil {
			log.Print(err)
			continue
		}

		if chk.Type != msgType || chk.Sensor != sensor || chk.Channel != channel {
			log.Printf("requested %s/%d/%d, current %s/%d/%d",
				msgType, sensor, channel,
				chk.Type, chk.Sensor, chk.Channel)
			continue
		}

		log.Printf("write [%s/%d/%d]: %s", msgType, sensor, channel, string(msg))

		err = c.socket.WriteMessage(websocket.TextMessage, msg)
		if err != nil {
			log.Printf("socket write error: %s\n", err)
			continue
		}
	}
}

func upgrade(w http.ResponseWriter, r *http.Request, msgRelay *relay.Relay, msgType string, sensor, channel int) {
	socket, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Print(err)
		return
	}

	forward := make(chan []byte)
	relayClient := relay.NewClient(forward)
	msgRelay.ListenerAdd(relayClient)

	client := &client{
		socket:  socket,
		send:    forward,
		msgType: msgType,
		sensor:  sensor,
		channel: channel,
	}

	go client.write()
	client.read()

	close(forward)
	msgRelay.ListenerRemove(relayClient)
}

func isDataType(data io.ReadCloser, dataType string) ([]byte, bool) {
	body, err := ioutil.ReadAll(data)
	if err != nil {
		log.Print(err)
		return nil, false
	}

	var msg message
	err = json.Unmarshal(body, &msg)
	if err != nil {
		log.Print(err)
		return nil, false
	}

	if msg.Type != dataType {
		log.Printf("unknown type: %s", msg.Type)
		return nil, false
	}

	return body, true
}

func urlQueryParamInt(r *http.Request, key string, defaultValue int) int {
	value, ok := r.URL.Query()[key]
	if !ok || len(value[0]) < 1 {
		log.Printf("url param %s is missing, using default %d", key, defaultValue)
		return defaultValue
	}

	i, err := strconv.Atoi(value[0])
	if err != nil {
		log.Printf("failed to parse url qurey param %s", key)
		return defaultValue
	}

	return i
}

func urlQueryParamStr(r *http.Request, key string, defaultValue string) string {
	value, ok := r.URL.Query()[key]
	if !ok || len(value[0]) < 1 {
		log.Printf("url param %s is missing, using default %s", key, defaultValue)
		return defaultValue
	}

	return value[0]
}

func data(msgRelay *relay.Relay, dataTypes *DataTypes) func(w http.ResponseWriter, r *http.Request) {
	return func(w http.ResponseWriter, r *http.Request) {
		log.Print("handling data")

		switch r.Method {
		case "GET":
			t := urlQueryParamStr(r, "type", "octavebands")
			s := urlQueryParamInt(r, "sensor", 0)
			c := urlQueryParamInt(r, "channel", 0)

			if !dataTypes.IsValidType(t) {
				log.Printf("unknown type requested: %s", t)
				t = "octavebands"
			}

			log.Printf("data get: type %s, sensor %d, channel %d", t, s, c)

			upgrade(w, r, msgRelay, t, s, c)
		case "POST":
			log.Println("data post")

			body, ok := dataTypes.IsValidDataType(r.Body)
			if !ok {
				return
			}
			msgRelay.Broadcast(body)
		}
	}
}

type templateHandler struct {
	once      sync.Once
	directory string
	filename  string
	templ     *template.Template
}

func (t *templateHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	log.Printf("server host: %s\n", r.Host)

	t.once.Do(func() {
		t.templ = template.Must(template.ParseFiles(filepath.Join(t.directory, t.filename)))
	})
	t.templ.Execute(w, nil)
}

func httpListener(port, staticDir string) {
	http.Handle("/", &templateHandler{directory: staticDir, filename: "index.html"})

	dataTypes := NewDataTypes([]string{"octavebands", "frequencybins"})
	dataRelay := relay.NewRelay()
	handleData := data(dataRelay, dataTypes)
	http.HandleFunc("/data", handleData)

	colonPort := ":" + port
	err := http.ListenAndServe(colonPort, nil)
	if err != nil {
		log.Fatal(err)
	}
}

func main() {
	httpPort := flag.String("http", "5100", "http port")
	staticDir := flag.String("static", "", "path to directory containing `index.html`")

	flag.Parse()

	if *staticDir == "" {
		log.Fatal("-static required")
	}

	httpListener(*httpPort, *staticDir)
}
