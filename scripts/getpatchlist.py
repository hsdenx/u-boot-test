#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+

import requests
import argparse
from bs4 import BeautifulSoup

# URLs
LOGIN_URL = "https://patchwork.ozlabs.org/user/login/"
TODO_LIST_URL = "https://patchwork.ozlabs.org/user/todo/uboot/"

def login_and_get_cookies(username, password):
    # Start a session to maintain cookies
    session = requests.Session()

    # Get the CSRF token by sending a GET request to the login page
    response = session.get(LOGIN_URL)

    # Check if the response is successful
    if response.status_code != 200:
        print(f"Error fetching login page: {response.status_code}")
        return None, None

    # Extract CSRF token from cookies or HTML
    csrf_token = response.cookies.get('csrftoken')
    if not csrf_token:
        # If not found in cookies, extract it from the HTML form
        soup = BeautifulSoup(response.text, "html.parser")
        csrf_token = soup.find("input", {"name": "csrfmiddlewaretoken"})["value"]

    #print(f"CSRF Token: {csrf_token}")

    # Prepare login data
    login_data = {
        'username': username,
        'password': password,
        'csrfmiddlewaretoken': csrf_token,
    }

    # Send POST request to login
    headers = {
        'User-Agent': 'Mozilla/5.0',  # Common user-agent string
        'Referer': LOGIN_URL  # Set Referer header to avoid potential issues
    }

    # Perform login
    login_response = session.post(LOGIN_URL, data=login_data, headers=headers)

    # If login is successful, extract session ID from cookies
    if login_response.status_code == 200:
        session_id = session.cookies.get('sessionid')  # Get session ID from cookies
        #print(f"Session ID: {session_id}")
        return session, session_id
    else:
        print(f"Login failed: {login_response.status_code}")
        return None, None

def get_todo_patch_and_message_ids(session):
    response = session.get(TODO_LIST_URL)

    if response.status_code != 200:
        print(f"Error fetching todo list: {response.status_code}")
        return []

    soup = BeautifulSoup(response.text, "html.parser")

    patch_data = []
    for row in soup.select("tr[id^='patch_row']"):  # Identify all patch rows
        # Extract the Patch ID from the 'id' attribute
        patch_id = row.get("id").split(":")[1]

        # Extract the Message ID from the patch URL
        msg_id_tag = row.select_one('a[href^="/project/uboot/patch/"]')  # Look for the patch URL
        if msg_id_tag:
            msg_id = msg_id_tag["href"].split("/")[4]  # Extract the Message ID

            patch_data.append((patch_id, msg_id))

    return patch_data

def main():
    """
    Get a list of your patches in your patchworks ToDo list. List for each patch the
    PATCH ID and MESSAGE ID, so you can use them for example with the pwclient.py tool.

    Or you can do in your current U-Boot tree:

    MIL=$(scripts/getpatchlist.py <you username> <you patchwork password> | cut -d " " -f 2)
    for m in $MIL;do
        rm -rf mbox
        wget http://patchwork.ozlabs.org/project/uboot/patch/$m/mbox
        scripts/checkpatch.pl mbox
        git am -3 --whitespace=strip mbox
    done

    And you have all your patches from your ToDo list download, checked with the
    checkpatch.pl script and applied to your current branch.
    """
    # Set up argument parser
    parser = argparse.ArgumentParser(description="Login to Patchwork and get Todo list patches.")
    parser.add_argument("username", help="Your Patchwork username")
    parser.add_argument("password", help="Your Patchwork password")

    # Parse arguments
    args = parser.parse_args()

    # Step 1: Login and get session
    session, session_id = login_and_get_cookies(args.username, args.password)

    if session:
        # Step 2: Get patch IDs and message IDs
        patches = get_todo_patch_and_message_ids(session)

        if patches:
            # print("Found Patch IDs and Message IDs:")
            for patch_id, msg_id in patches:
                print(f"{patch_id} {msg_id}")
        else:
            print("No patches found. Check the HTML structure!")
            exit (1)
    else:
        print("Unable to login. Exiting.")
        exit (2)

if __name__ == "__main__":
    main()

