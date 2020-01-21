// author john.d.sheehan@ie.ibm.com

package relay

// Client relay client, receives broadcast data
type Client struct {
	c chan []byte
}

// NewClient return new instance of client
func NewClient(c chan []byte) *Client {
	return &Client{
		c: c,
	}
}

// Relay handles broadcasting of data to connected clients
type Relay struct {
	forward chan []byte
	join    chan *Client
	leave   chan *Client
	clients map[*Client]bool
}

// NewRelay return new instance of relay, and start the relay loop
func NewRelay() *Relay {
	r := &Relay{
		forward: make(chan []byte),
		join:    make(chan *Client),
		leave:   make(chan *Client),
		clients: make(map[*Client]bool),
	}

	go r.run()

	return r
}

// ListenerAdd add client
func (r *Relay) ListenerAdd(c *Client) {
	r.join <- c
}

// ListenerRemove remove client
func (r *Relay) ListenerRemove(c *Client) {
	r.leave <- c
}

// Broadcast data to all connected clients
func (r *Relay) Broadcast(b []byte) {
	r.forward <- b
}

func (r *Relay) run() {
	for {
		select {
		case client := <-r.join:
			r.clients[client] = true
		case client := <-r.leave:
			delete(r.clients, client)
		case f := <-r.forward:
			for client := range r.clients {
				client.c <- f
			}
		}
	}
}
