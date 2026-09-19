"""Operator client with a graphical interface (tkinter, standard library).

Same features as operator_cli.py over the same TelepConnection: active nodes,
last measurements, one-node query, alerts and system status. Refreshes every
REFRESH_MS and on the "Refresh" button.
"""
import argparse
import sys
import time
import tkinter as tk
from tkinter import messagebox, ttk

import telep

REFRESH_MS = 3000
MEASUREMENT_COLUMNS = ("TEMP", "HUM", "POWER", "VIB", "STATUS")


def format_timestamp(epoch_text):
    return time.strftime("%H:%M:%S", time.localtime(int(epoch_text)))


class OperatorWindow:
    def __init__(self, root, connection, hostname):
        self.root = root
        self.connection = connection
        root.title(f"TELEP/1.0 operator - {hostname}")
        root.geometry("900x560")
        self.build_widgets()
        self.refresh()

    # Toolbar on top, two tables below.
    def build_widgets(self):
        toolbar = ttk.Frame(self.root, padding=6)
        toolbar.pack(fill="x")
        ttk.Button(toolbar, text="Refresh", command=self.refresh).pack(side="left")
        ttk.Label(toolbar, text="   Query node:").pack(side="left")
        self.node_entry = ttk.Entry(toolbar, width=14)
        self.node_entry.pack(side="left")
        ttk.Button(toolbar, text="GET_LAST", command=self.query_single_node).pack(side="left", padx=4)
        self.status_label = ttk.Label(toolbar, text="", foreground="gray")
        self.status_label.pack(side="right")
        self.last_alert_label = ttk.Label(self.root, text="", foreground="red", padding=(8, 0))
        self.last_alert_label.pack(fill="x")

        panes = ttk.PanedWindow(self.root, orient="vertical")
        panes.pack(fill="both", expand=True, padx=6, pady=6)

        node_columns = ("id", "state", "ago", *MEASUREMENT_COLUMNS)
        self.node_table = self.make_table(panes, "Nodes and last measurements", node_columns)
        self.alert_table = self.make_table(panes, "Alerts", ("time", "node", "type", "value"))

    def make_table(self, parent, title, columns):
        frame = ttk.LabelFrame(parent, text=title, padding=4)
        table = ttk.Treeview(frame, columns=columns, show="headings", height=8)
        for column in columns:
            table.heading(column, text=column.upper())
            table.column(column, width=90, anchor="center")
        table.tag_configure("INACTIVE", foreground="gray")
        table.tag_configure("ALERT", foreground="red")
        table.pack(fill="both", expand=True)
        parent.add(frame, weight=1)
        return table

    @staticmethod
    def replace_rows(table, rows):
        table.delete(*table.get_children())
        for values, tag in rows:
            table.insert("", "end", values=values, tags=(tag,))

    def request(self, command, *fields):
        """Sends a command; on a network error shows a dialog and closes the window."""
        try:
            return self.connection.request(command, *fields)
        except (OSError, ConnectionError) as error:
            messagebox.showerror("Connection lost", str(error))
            self.root.destroy()
            return None

    def refresh(self):
        pushed = self.connection.take_alerts()
        if pushed:
            _, node_id, alert_type, value = pushed[-1]
            self.last_alert_label.config(text=f"!! ALERT received: {node_id} {alert_type} {value}")
        status = self.request("GET_STATUS")
        if status is None:
            return
        self.status_label.config(text=status[0][1].replace(";", "   ") if status[0][0] == "OK" else status[0])

        node_rows = []
        for node_id, state, seconds_ago in self.request("GET_NODES")[1:]:
            last = self.request("GET_LAST", node_id)[0]
            measurements = telep.decode_measurements(last[3]) if last[0] == "OK" else {}
            values = (node_id, state, seconds_ago, *(measurements.get(name, "-") for name in MEASUREMENT_COLUMNS))
            node_rows.append((values, state))
        self.replace_rows(self.node_table, node_rows)

        alert_rows = [((format_timestamp(ts), node_id, alert_type, value), "ALERT")
                      for ts, node_id, alert_type, value in reversed(self.request("GET_ALERTS")[1:])]
        self.replace_rows(self.alert_table, alert_rows)

        self.root.after(REFRESH_MS, self.refresh)

    def query_single_node(self):
        node_id = self.node_entry.get().strip()
        if not node_id:
            return
        reply = self.request("GET_LAST", node_id)
        if reply is None:
            return
        line = reply[0]
        if line[0] != "OK":
            messagebox.showwarning("Server error", f"{line[1]}: {line[2]}")
        elif line[3] == "NO_DATA":
            messagebox.showinfo(node_id, "No measurements yet")
        else:
            text = "\n".join(f"{name}: {value}" for name, value in telep.decode_measurements(line[3]).items())
            messagebox.showinfo(f"{node_id} - {format_timestamp(line[2])}", text)


def main():
    parser = argparse.ArgumentParser(description="Graphical operator (TELEP/1.0)")
    parser.add_argument("--host", default=telep.DEFAULT_HOST, help="server DNS name")
    parser.add_argument("--port", type=int, default=telep.TCP_PORT)
    args = parser.parse_args()

    connection = telep.TelepConnection(args.host, args.port)
    try:
        connection.connect()
        connection.subscribe()
    except OSError as error:
        print(f"could not connect to {args.host}:{args.port}: {error}", file=sys.stderr)
        sys.exit(1)

    root = tk.Tk()
    OperatorWindow(root, connection, args.host)
    try:
        root.mainloop()
    finally:
        connection.close()


if __name__ == "__main__":
    main()
