from playwright.sync_api import sync_playwright

def verify_frontend():
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()

        # Navigate to the served build
        page.goto("http://localhost:8081")

        # Wait for title
        page.wait_for_selector("text=PETRICHOR")

        # Take screenshot
        page.screenshot(path="frontend_verification.png")

        browser.close()

if __name__ == "__main__":
    verify_frontend()
