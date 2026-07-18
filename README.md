# KioskTUI

An application to track purchased items in the Online Kiosk. Uses NCurses and Zettle API to display the information with python helper scripts.

## Dependencies

To use the TUI, you should have installed and assured that the following are working:

1. gcc
2. g++
3. NCurses
4. Sqllite
5. python

The software has not been tested on windows, as the windows terminal emulator infrastructure has really bad graphical protocols/compatibility.
MacOS should work (UNIX based, iTERM's graphical protocol etc).
It has also been tested on server configurations with no DE or compositor, and everything runs properly, though the graphical borders get converted to true ascii characters instead.

## Use

To use the TUI:

The database must be initialized before synchronization can work

```bash
make db
```

When db init is successful, you can sync it (this requires Zettle API keys placed in the .env)

```bash
make sync
```

The script keeps running until interruption, so it is recommended to use tmux or another type of terminal multiplexer to the sync script in a different pane, and use another for the tui itself.

Finally the binaries can be built and the application launched in the terminal

```bash
make && ./leaderboard
```

## Automation

For it to run persistently on a machine, a systemd service file for leaderboard and sync has been added. Since it uses systemd specific configurations, if the linux distribution uses another init system, it won't work out of the box. An equivalent service definition would have to be created for these systems.
Before enabling the service, change the values in both service files to:

User= to the user account that should run the application.
WorkingDirectory= to the directory containing the application.
ExecStart= to the full path of the executable.

Afterwards copy the service file to /etc/systemd/system/

```bash
sudo cp leaderboard.service /etc/systemd/system/
sudo cp sync.service /etc/systemd/system/
```

Then reload systemd and enable the service

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now leaderboard.service
sudo systemctl enable --now sync.service
```

To confirm that it works, run:

```bash
sudo systemctl status leaderboard.service
sudo systemctl status sync.service
```

## Testing

Run the testing framework using:

```bash
make test
```

The tests written emulate Zettle transactions and are meant to test response time of the tui as well as edge cases (such as how it responds to max items in a transaction). All the injected test data can be removed after observation with a prompt after test finish.
