# Creating a Custom WSL Ubuntu 24.04 Image with Automatic OOBE

This guide outlines the complete, tested workflow for installing a fresh instance of Ubuntu 24.04 on WSL, stripping it of its initial user data, embedding an automated setup trap, and exporting it as a pristine `.tar` image. 

When this exported image is imported to any machine, the very first launch will automatically trigger the official Canonical Out-of-Box Experience (OOBE) wizard to securely provision a new default user.

---

## Part 1: Install and Clean the Base Image

Because the Microsoft Store automatically runs the OOBE on the first install, we must complete it with a throwaway user and then carefully scrub the instance clean.

### 1. Install Ubuntu and create a temporary user
```powershell
wsl --install -d Ubuntu-24.04
```
*When the terminal window pops up, follow the prompts to create a temporary user (e.g., `temp`) and complete the installation.*

### 2. Remove the Default User Configuration
Log into the instance as the `root` user to bypass the standard login:
```powershell
wsl -d Ubuntu-24.04 -u root
```
Open the WSL configuration file:
```bash
sudo nano /etc/wsl.conf
```
Find the `[user]` section and **delete** the line that says `default=temp`. Save (`Ctrl+O`, `Enter`) and exit nano (`Ctrl+X`).

Exit the shell:
```bash
exit
```

### 3. Terminate and Delete the Temporary User
Because Linux runs background services (via `systemd`) for the default user, we must completely shut down the WSL instance to kill those services before we can delete the user.

In PowerShell:
```powershell
wsl --terminate Ubuntu-24.04
```
Boot back into root:
```powershell
wsl -d Ubuntu-24.04 -u root
```
Wipe the temporary user and their home directory entirely:
```bash
deluser --remove-home temp
```

> [!TIP]
> If you get a "user is currently used by process" error, ensure you successfully removed the user from `/etc/wsl.conf` and remembered to run `wsl --terminate` before returning to root.

---

## Part 2: Inject the OOBE Setup Trap

Now that the image is clean, we will create a trap in root's `.profile`. Because generic `.tar` imports default to the `root` user, this profile script is guaranteed to run on the very first launch. 

Run this command block inside your root shell:

```bash
cat << '__EOF__' > /root/.profile
# 1. Run the Ubuntu setup wizard
/usr/lib/wsl/wsl-setup

# 2. Find the new username it just created
NEW_USER=$(id -nu 1000 2>/dev/null)

if [ -n "$NEW_USER" ]; then
    # 3. Safely wipe any leftover [user] lines from the config file
    sed -i '/\[user\]/d' /etc/wsl.conf
    sed -i '/default=/d' /etc/wsl.conf
    
    # 4. Set the true default user so VS Code respects it!
    echo -e "\n[user]\ndefault=$NEW_USER" >> /etc/wsl.conf
    
    # 5. Delete this trap so it never runs again
    rm ~/.profile
    
    # 6. Switch to the new user for this session
    exec su - "$NEW_USER"
fi
__EOF__
```

### Clean History and Exit
Clear out the temporary folder and wipe your bash history so the exported image is immaculate:
```bash
rm -rf tmp/*
history -c && history -w && exit
```

---

## Part 3: Export the Pristine Image

Back in Windows PowerShell, terminate the instance one last time to ensure all files are flushed to disk, and export your masterpiece.

```powershell
wsl --terminate Ubuntu-24.04
wsl --export Ubuntu-24.04 Ubuntu-24.04-clean.tar.xz
```
*(You can use `.tar` or `.tar.xz` depending on your compression preferences and Windows version).*

> [!IMPORTANT]
> Your `Ubuntu-24.04-clean` file is now a reusable template. Keep it safe! You can use it to spin up as many independent sandboxes as you want.

---

## Part 4: Import and Provision a New Sandbox

To test your new image (or whenever you need a new environment), import it with a custom name:

```powershell
wsl --import Ubuntu-24.04-test <INSTALLATION ROUTE>\Ubuntu-24.04-test .\Ubuntu-24.04-clean.tar.xz
```

Launch the new sandbox:
```powershell
wsl -d Ubuntu-24.04-test
```

**The Magic Happens Here:** 
1. The sandbox boots and defaults to `root`.
2. The `/root/.profile` script immediately runs.
3. The Ubuntu OOBE wizard takes over the screen.
4. You create your new definitive user.
5. The profile script automatically sets your user as the true default in `/etc/wsl.conf`.
6. The script deletes itself and drops you seamlessly into your new account.

### Final Housekeeping
Because the script used `su -` to switch accounts for that initial session, you might want to ensure your shell always lands in your home directory on future boots:
```bash
echo "cd ${HOME}" >> ${HOME}/.bashrc
exit
```

Terminate the sandbox so WSL can natively read the new `wsl.conf` user configuration on the next cold boot:
```powershell
wsl --terminate Ubuntu-24.04-test
```

The next time you type `wsl -d Ubuntu-24.04-test`, you will be logged directly into your new user account, ready to work!
