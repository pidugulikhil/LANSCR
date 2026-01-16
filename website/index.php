<?php
// LANSCR - Official Landing Page
// Single PHP/HTML file with project info + multi-version downloads.

// Project links
$github_repo_url = 'https://github.com/pidugulikhil/LANSCR';
$github_owner_url = 'https://github.com/pidugulikhil';
$github_releases_url = $github_repo_url . '/releases';
$github_release_asset_url = $github_repo_url . '/releases/download/application/LANSCR.exe';
$github_issues_url = $github_repo_url . '/issues';
$github_pulls_url = $github_repo_url . '/pulls';
$github_license_url = $github_repo_url . '/blob/main/LICENSE';
$github_readme_url = $github_repo_url . '#readme';

function h($value) {
    return htmlspecialchars((string)$value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function release_id($tag) {
    return trim(preg_replace('/[^a-zA-Z0-9]+/', '-', (string)$tag), '-');
}

// Add versions here when you upload new builds.
// Recommended approach:
// - Put versioned assets in ./downloads/ (example: downloads/LANSCR-v1.0.0.zip)
// - Keep GitHub Releases as the canonical source for downloads.
$releases = [
    [
        'tag' => 'application',
        'channel' => 'Latest',
        'release_date' => '2026-01-16',
        'assets' => [
            ['label' => 'Download LANSCR.exe (Windows)', 'href' => $github_release_asset_url],
            ['label' => 'GitHub release page', 'href' => $github_releases_url . '/tag/application'],
            ['label' => 'All releases', 'href' => $github_releases_url],
        ],
        'highlights' => [
            'One EXE: GUI launcher + CLI modes',
            'Screen streaming: MJPEG over HTTP (browser + native viewer)',
            'Optional system-audio streaming (WASAPI loopback)',
            'Private mode (username/password) available',
        ],
    ],
    // Add older versions here if you publish versioned tags (e.g. v1.0.0, v1.1.0)
];

$latest_release = $releases[0];

$cli_help_text = <<<'TXT'
Usage:
  LANSCR.exe [-v|--verbose] [--mute-audio] [--no-audio] server <port> [fps] [jpegQuality0to100]
  LANSCR.exe [-v|--verbose] [--mute] client <url>
  LANSCR.exe [-v|--verbose] udp-server <port> [fps] [jpegQuality0to100]
  LANSCR.exe [-v|--verbose] udp-client <serverIp> <port>
  LANSCR.exe audio-mute <urlOrPort> <0|1>
  LANSCR.exe stop <port>
  LANSCR.exe detect

Examples:
  LANSCR.exe server 8000 10 80
  LANSCR.exe -v server 80 80 80
  LANSCR.exe client http://192.168.1.50:8000/
  LANSCR.exe --mute client http://192.168.1.50:8000/
  LANSCR.exe udp-server 9000 60 70
  LANSCR.exe udp-client 192.168.1.50 9000
  LANSCR.exe audio-mute 8000 1
  LANSCR.exe stop 8000
TXT;

// Navigation items
$nav_items = [
    'Home' => '#home',
    'Features' => '#features',
    'Getting Started' => '#getting-started',
    'Downloads' => '#downloads',
    'Documentation' => '#documentation',
    'Contribute' => '#contribute',
    'Development' => '#development',
    'Privacy' => '#privacy',
    'Support' => '#support',
    'License' => '#license',
    'Releases' => $github_releases_url,
    'GitHub' => $github_repo_url,
];

// Sidebar grouping (keeps the nav feeling like a real product page)
$nav_groups = [
    'Product' => ['Home', 'Features', 'Getting Started', 'Downloads', 'Documentation'],
    'Project' => ['Contribute', 'Development', 'Privacy', 'Support', 'License'],
    'Links' => ['Releases', 'GitHub'],
];
?>

<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>LANSCR - Local Network Screen & Audio Sharing</title>
    <link rel="icon" type="image/x-icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><rect width='100' height='100' fill='%232864fa'/><circle cx='50' cy='40' r='20' fill='white'/><path d='M30,70 L70,70 L50,90 Z' fill='white'/></svg>">
    
    <!-- Fonts -->
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&display=swap" rel="stylesheet">
    
    <style>
        :root {
            /* Premium green + sky-blue theme */
            --primary: #12c780;
            --primary-dark: #0ea06a;
            --accent: #22b8ff;
            --accent-dark: #0b9ddf;
            --secondary: #5f6b7a;
            --success: #12c780;
            --danger: #dc3545;
            --light: #f8f9fa;
            --dark: #212529;
            --gray: #6c757d;
            --gray-light: #e9ecef;
            --card: rgba(255, 255, 255, 0.92);
            --border-radius: 8px;
            --box-shadow: 0 10px 30px rgba(2, 12, 27, 0.08);
            --transition: all 0.3s ease;
        }

        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: 'Inter', sans-serif;
            line-height: 1.6;
            color: var(--dark);
            background: radial-gradient(1200px 600px at 10% 0%, rgba(18, 199, 128, 0.10), transparent 55%),
                        radial-gradient(900px 500px at 90% 20%, rgba(34, 184, 255, 0.12), transparent 60%),
                        linear-gradient(135deg, #f5fbf8 0%, #f3f9ff 100%);
            min-height: 100vh;
        }

        /* Layout */
        :root {
            --sidebar-width: 292px;
            --sidebar-width-collapsed: 84px;
            --topbar-height: 64px;
        }

        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 0 2rem;
        }

        .app-shell {
            display: grid;
            grid-template-columns: var(--sidebar-width) 1fr;
            min-height: 100vh;
        }

        .main {
            min-width: 0;
        }

        .main .container {
            max-width: 1100px;
        }

        /* Sidebar */
        .sidebar {
            position: sticky;
            top: 0;
            height: 100vh;
            padding: 1.25rem 1rem;
            background: rgba(255, 255, 255, 0.80);
            backdrop-filter: blur(14px);
            border-right: 1px solid rgba(33, 37, 41, 0.08);
            display: flex;
            flex-direction: column;
            gap: 1rem;
            z-index: 1000;
        }

        .sidebar-top {
            display: flex;
            align-items: center;
            justify-content: space-between;
            gap: 0.75rem;
        }

        .logo {
            display: flex;
            align-items: center;
            gap: 12px;
            font-size: 1.5rem;
            font-weight: 700;
            color: var(--primary);
            text-decoration: none;
        }

        .logo-icon {
            width: 40px;
            height: 40px;
            background: linear-gradient(135deg, var(--primary) 0%, var(--accent) 100%);
            border-radius: 10px;
            display: flex;
            align-items: center;
            justify-content: center;
            color: white;
            font-weight: bold;
            font-size: 1.2rem;
        }

        .sidebar-toggle {
            appearance: none;
            border: 1px solid rgba(33, 37, 41, 0.10);
            background: rgba(255, 255, 255, 0.70);
            color: var(--dark);
            border-radius: 12px;
            width: 44px;
            height: 44px;
            display: none;
            align-items: center;
            justify-content: center;
            cursor: pointer;
            transition: var(--transition);
        }

        .sidebar-toggle:hover {
            transform: translateY(-1px);
            border-color: rgba(18, 199, 128, 0.35);
            box-shadow: 0 8px 18px rgba(2, 12, 27, 0.08);
        }

        .sidebar-nav {
            padding: 0.25rem;
            overflow: auto;
        }

        .nav-section {
            padding: 0.75rem 0.5rem;
            border-top: 1px solid rgba(33, 37, 41, 0.08);
        }

        .nav-section:first-child {
            border-top: none;
            padding-top: 0.25rem;
        }

        .nav-section-title {
            font-size: 0.78rem;
            letter-spacing: 0.08em;
            text-transform: uppercase;
            color: rgba(33, 37, 41, 0.55);
            margin: 0 0.25rem 0.5rem;
        }

        .nav-item {
            display: flex;
            align-items: center;
            gap: 0.75rem;
            padding: 0.65rem 0.75rem;
            margin: 0.15rem 0;
            border-radius: 12px;
            text-decoration: none;
            color: rgba(33, 37, 41, 0.86);
            font-weight: 600;
            transition: var(--transition);
        }

        .nav-item:hover {
            background: rgba(18, 199, 128, 0.10);
            color: rgba(33, 37, 41, 0.95);
        }

        .nav-item[aria-current="page"] {
            background: linear-gradient(135deg, rgba(18, 199, 128, 0.18) 0%, rgba(34, 184, 255, 0.16) 100%);
            border: 1px solid rgba(18, 199, 128, 0.24);
        }

        .nav-item.external::after {
            content: '↗';
            margin-left: auto;
            color: rgba(33, 37, 41, 0.45);
            font-weight: 700;
        }

        .sidebar-cta {
            margin-top: auto;
            padding: 0.75rem 0.25rem 0;
            border-top: 1px solid rgba(33, 37, 41, 0.08);
            display: grid;
            gap: 0.6rem;
        }

        .btn.btn-small {
            justify-content: center;
            width: 100%;
            padding: 0.75rem 1rem;
        }

        .sidebar-backdrop {
            display: none;
            position: fixed;
            inset: 0;
            background: rgba(2, 12, 27, 0.38);
            backdrop-filter: blur(2px);
            z-index: 999;
        }

        /* Mobile topbar (shows only on small screens) */
        .mobile-topbar {
            display: none;
            position: sticky;
            top: 0;
            z-index: 900;
            height: var(--topbar-height);
            background: rgba(255, 255, 255, 0.85);
            backdrop-filter: blur(12px);
            border-bottom: 1px solid rgba(33, 37, 41, 0.08);
        }

        .mobile-topbar-inner {
            height: 100%;
            display: flex;
            align-items: center;
            justify-content: space-between;
            gap: 0.75rem;
        }

        /* Hero Section */
        .hero {
            padding: 6rem 0;
            text-align: center;
            background: linear-gradient(135deg, var(--primary) 0%, var(--accent) 100%);
            color: white;
            border-radius: 0 0 40px 40px;
            margin-bottom: 4rem;
            position: relative;
            overflow: hidden;
        }

        .hero::before {
            content: '';
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: url('data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" preserveAspectRatio="none"><path d="M0,0 L100,0 L100,100 Z" fill="white" opacity="0.07"/></svg>');
            background-size: cover;
        }

        .hero h1 {
            font-size: 3.5rem;
            margin-bottom: 1rem;
            font-weight: 700;
        }

        .hero p {
            font-size: 1.2rem;
            max-width: 700px;
            margin: 0 auto 2rem;
            opacity: 0.9;
        }

        .version-badge {
            display: inline-block;
            background: rgba(255, 255, 255, 0.2);
            padding: 0.5rem 1.5rem;
            border-radius: 50px;
            font-size: 0.9rem;
            margin-bottom: 2rem;
            backdrop-filter: blur(10px);
        }

        .cta-buttons {
            display: flex;
            gap: 1rem;
            justify-content: center;
            margin-top: 2rem;
        }

        .btn {
            padding: 0.8rem 2rem;
            border-radius: var(--border-radius);
            text-decoration: none;
            font-weight: 600;
            transition: var(--transition);
            display: inline-flex;
            align-items: center;
            gap: 0.5rem;
            border: 2px solid transparent;
        }

        .btn-primary {
            background: white;
            color: var(--primary);
        }

        .btn-primary:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 12px rgba(0, 0, 0, 0.15);
        }

        .btn-secondary {
            background: transparent;
            color: white;
            border-color: rgba(255, 255, 255, 0.3);
        }

        .btn-secondary:hover {
            background: rgba(255, 255, 255, 0.1);
            transform: translateY(-2px);
        }

        /* Features Section */
        .section {
            padding: 4rem 0;
        }

        .section-title {
            text-align: center;
            margin-bottom: 3rem;
        }

        .section-title h2 {
            font-size: 2.5rem;
            color: var(--dark);
            margin-bottom: 1rem;
        }

        .section-title p {
            color: var(--gray);
            max-width: 600px;
            margin: 0 auto;
        }

        .features-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 2rem;
            margin-bottom: 3rem;
        }

        .feature-card {
            background: white;
            border-radius: var(--border-radius);
            padding: 2rem;
            box-shadow: var(--box-shadow);
            transition: var(--transition);
            border: 1px solid var(--gray-light);
        }

        .feature-card:hover {
            transform: translateY(-5px);
            box-shadow: 0 8px 16px rgba(0, 0, 0, 0.1);
        }

        .feature-icon {
            width: 60px;
            height: 60px;
            background: var(--primary);
            border-radius: 12px;
            display: flex;
            align-items: center;
            justify-content: center;
            margin-bottom: 1.5rem;
            color: white;
            font-size: 1.5rem;
        }

        .feature-card h3 {
            margin-bottom: 1rem;
            color: var(--dark);
        }

        .feature-card p {
            color: var(--gray);
            margin-bottom: 1rem;
        }

        .feature-list {
            list-style: none;
            margin-top: 1rem;
        }

        .feature-list li {
            padding: 0.3rem 0;
            padding-left: 1.5rem;
            position: relative;
            color: var(--gray);
        }

        .feature-list li::before {
            content: '✓';
            position: absolute;
            left: 0;
            color: var(--success);
            font-weight: bold;
        }

        /* Download Section */
        .download-section {
            background: white;
            border-radius: var(--border-radius);
            padding: 3rem;
            box-shadow: var(--box-shadow);
            margin-bottom: 3rem;
        }

        .download-card {
            text-align: center;
            padding: 2rem;
            border: 2px dashed var(--gray-light);
            border-radius: var(--border-radius);
            margin-bottom: 2rem;
        }

        .download-info {
            display: flex;
            justify-content: center;
            gap: 3rem;
            margin: 2rem 0;
            flex-wrap: wrap;
        }

        .info-item {
            text-align: center;
        }

        .info-item .value {
            font-size: 1.5rem;
            font-weight: 600;
            color: var(--primary);
            display: block;
        }

        .info-item .label {
            color: var(--gray);
            font-size: 0.9rem;
        }

        .download-options {
            display: flex;
            gap: 1rem;
            justify-content: center;
            flex-wrap: wrap;
        }

        /* Getting Started */
        .steps {
            counter-reset: step;
            max-width: 800px;
            margin: 0 auto;
        }

        .step {
            background: white;
            border-radius: var(--border-radius);
            padding: 2rem;
            margin-bottom: 1.5rem;
            box-shadow: var(--box-shadow);
            position: relative;
            padding-left: 5rem;
        }

        .step::before {
            counter-increment: step;
            content: counter(step);
            position: absolute;
            left: 2rem;
            top: 2rem;
            width: 40px;
            height: 40px;
            background: var(--primary);
            color: white;
            border-radius: 50%;
            display: flex;
            align-items: center;
            justify-content: center;
            font-weight: bold;
        }

        .step h3 {
            margin-bottom: 0.5rem;
            color: var(--dark);
        }

        .code-block {
            background: var(--dark);
            color: var(--light);
            padding: 1rem;
            border-radius: var(--border-radius);
            font-family: monospace;
            margin: 1rem 0;
            overflow-x: auto;
        }

        /* Version History */
        .version-tabs {
            display: flex;
            gap: 1rem;
            margin-bottom: 2rem;
            border-bottom: 2px solid var(--gray-light);
            padding-bottom: 1rem;
        }

        .version-tab {
            padding: 0.8rem 1.5rem;
            background: none;
            border: none;
            border-radius: var(--border-radius);
            cursor: pointer;
            font-weight: 500;
            transition: var(--transition);
        }

        .version-tab.active {
            background: var(--primary);
            color: white;
        }

        .version-content {
            display: none;
            animation: fadeIn 0.5s ease;
        }

        .version-content.active {
            display: block;
        }

        /* Footer */
        footer {
            background: var(--dark);
            color: white;
            padding: 4rem 0 2rem;
            margin-top: 4rem;
        }

        .footer-content {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 3rem;
            margin-bottom: 3rem;
        }

        .footer-section h3 {
            margin-bottom: 1.5rem;
            color: white;
        }

        .footer-links {
            list-style: none;
        }

        .footer-links li {
            margin-bottom: 0.8rem;
        }

        .footer-links a {
            color: var(--gray-light);
            text-decoration: none;
            transition: var(--transition);
        }

        .footer-links a:hover {
            color: white;
        }

        .copyright {
            text-align: center;
            padding-top: 2rem;
            border-top: 1px solid rgba(255, 255, 255, 0.1);
            color: var(--gray);
        }

        /* Animations */
        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(20px); }
            to { opacity: 1; transform: translateY(0); }
        }

        .animate-in {
            animation: fadeIn 0.6s ease forwards;
        }

        /* Responsive Design */
        @media (max-width: 768px) {
            .app-shell {
                grid-template-columns: 1fr;
            }

            .sidebar {
                position: fixed;
                left: 0;
                top: 0;
                height: 100vh;
                width: min(var(--sidebar-width), 88vw);
                transform: translateX(-105%);
                transition: transform 260ms ease;
                box-shadow: 0 24px 70px rgba(2, 12, 27, 0.18);
            }

            body.sidebar-open .sidebar {
                transform: translateX(0);
            }

            .sidebar-backdrop {
                display: none;
            }

            body.sidebar-open .sidebar-backdrop {
                display: block;
            }

            .mobile-topbar {
                display: block;
            }

            .sidebar-toggle {
                display: inline-flex;
            }
            
            .hero h1 {
                font-size: 2.5rem;
            }
            
            .features-grid {
                grid-template-columns: 1fr;
            }
            
            .cta-buttons {
                flex-direction: column;
                align-items: center;
            }
            
            .step {
                padding-left: 2rem;
                padding-top: 4rem;
            }
            
            .step::before {
                top: 1.5rem;
                left: 1.5rem;
            }
        }
    </style>
</head>
<body>
    <div class="app-shell">
        <aside class="sidebar" id="sidebar" aria-label="Site navigation">
            <div class="sidebar-top">
                <a href="#home" class="logo">
                    <div class="logo-icon">L</div>
                    <span>LANSCR</span>
                </a>
                <button class="sidebar-toggle" type="button" data-action="toggleSidebar" aria-label="Open menu" aria-controls="sidebar" aria-expanded="false">
                    <svg width="20" height="20" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
                        <path d="M4 6H20" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
                        <path d="M4 12H20" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
                        <path d="M4 18H20" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
                    </svg>
                </button>
            </div>

            <nav class="sidebar-nav" aria-label="Primary">
                <?php foreach($nav_groups as $group_title => $labels): ?>
                    <div class="nav-section">
                        <div class="nav-section-title"><?php echo h($group_title); ?></div>
                        <?php foreach($labels as $label): ?>
                            <?php if (!isset($nav_items[$label])) continue; ?>
                            <?php $url = $nav_items[$label]; ?>
                            <?php $is_internal = is_string($url) && $url !== '' && $url[0] === '#'; ?>
                            <?php $is_external = !$is_internal; ?>
                            <a
                                class="nav-item <?php echo $is_external ? 'external' : ''; ?>"
                                href="<?php echo h($url); ?>"
                                <?php if ($is_external): ?>target="_blank" rel="noopener"<?php endif; ?>
                            >
                                <?php echo h($label); ?>
                            </a>
                        <?php endforeach; ?>
                    </div>
                <?php endforeach; ?>
            </nav>

            <div class="sidebar-cta">
                <a class="btn btn-primary btn-small" href="<?php echo h($github_release_asset_url); ?>">Download LANSCR.exe</a>
                <a class="btn btn-secondary btn-small" href="<?php echo h($github_repo_url); ?>" target="_blank" rel="noopener">Contribute on GitHub</a>
            </div>
        </aside>

        <div class="sidebar-backdrop" data-action="closeSidebar" aria-hidden="true"></div>

        <main class="main">
            <div class="mobile-topbar" aria-label="Mobile top bar">
                <div class="container mobile-topbar-inner">
                    <a href="#home" class="logo" style="font-size: 1.15rem;">
                        <div class="logo-icon" style="width: 34px; height: 34px; border-radius: 10px;">L</div>
                        <span>LANSCR</span>
                    </a>
                    <button class="sidebar-toggle" type="button" data-action="toggleSidebar" aria-label="Open menu" aria-controls="sidebar" aria-expanded="false">
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
                            <path d="M4 6H20" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
                            <path d="M4 12H20" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
                            <path d="M4 18H20" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
                        </svg>
                    </button>
                </div>
            </div>

    <!-- Hero Section -->
    <section id="home" class="hero animate-in">
        <div class="container">
            <div class="version-badge">Latest: <?php echo h($latest_release['tag']); ?> • Released <?php echo date('M j, Y', strtotime($latest_release['release_date'])); ?></div>
            <h1>LANSCR</h1>
            <p>High-performance local network screen sharing with audio streaming. Share your screen effortlessly with anyone on your local network.</p>
            <div class="cta-buttons">
                <a href="<?php echo h($github_release_asset_url); ?>" class="btn btn-primary">
                    <svg width="20" height="20" fill="currentColor" viewBox="0 0 16 16">
                        <path d="M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5z"/>
                        <path d="M7.646 11.854a.5.5 0 0 0 .708 0l3-3a.5.5 0 0 0-.708-.708L8.5 10.293V1.5a.5.5 0 0 0-1 0v8.793L5.354 8.146a.5.5 0 1 0-.708.708l3 3z"/>
                    </svg>
                    Download LANSCR.exe
                </a>
                <a href="<?php echo h($github_releases_url); ?>" class="btn btn-secondary">
                    <svg width="20" height="20" fill="currentColor" viewBox="0 0 16 16">
                        <path d="M3.5 14a.5.5 0 0 1-.5-.5V13h10v.5a.5.5 0 0 1-.5.5h-9z"/>
                        <path d="M3 2.5A.5.5 0 0 1 3.5 2h9a.5.5 0 0 1 .5.5V12H3V2.5z"/>
                    </svg>
                    All Releases
                </a>
                <a href="#getting-started" class="btn btn-secondary">
                    <svg width="20" height="20" fill="currentColor" viewBox="0 0 16 16">
                        <path d="M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14zm0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16z"/>
                        <path d="M6.271 5.055a.5.5 0 0 1 .52.038l3.5 2.5a.5.5 0 0 1 0 .814l-3.5 2.5A.5.5 0 0 1 6 10.5v-5a.5.5 0 0 1 .271-.445z"/>
                    </svg>
                    Get Started
                </a>
            </div>
        </div>
    </section>

    <!-- Features Section -->
    <section id="features" class="section">
        <div class="container">
            <div class="section-title animate-in">
                <h2>Powerful Features</h2>
                <p>Everything you need for local network screen sharing</p>
            </div>
            
            <div class="features-grid">
                <!-- Feature 1 -->
                <div class="feature-card animate-in" style="animation-delay: 0.1s">
                    <div class="feature-icon">
                        <svg width="30" height="30" fill="currentColor" viewBox="0 0 16 16">
                            <path d="M0 4a2 2 0 0 1 2-2h12a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2V4zm2-1a1 1 0 0 0-1 1v8a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1V4a1 1 0 0 0-1-1H2z"/>
                            <path d="M8 6a2 2 0 1 0 0 4 2 2 0 0 0 0-4z"/>
                        </svg>
                    </div>
                    <h3>Live screen streaming</h3>
                    <p>Share your Windows desktop live over your local network.</p>
                    <ul class="feature-list">
                        <li>Works in any modern browser</li>
                        <li>Native Windows viewer available</li>
                        <li>Multi-monitor (virtual screen) capture</li>
                        <li>Designed for low-latency LAN viewing</li>
                    </ul>
                </div>

                <!-- Feature 2 -->
                <div class="feature-card animate-in" style="animation-delay: 0.2s">
                    <div class="feature-icon">
                        <svg width="30" height="30" fill="currentColor" viewBox="0 0 16 16">
                            <path d="M8 3a5 5 0 0 0-5 5v4.5a.5.5 0 0 0 .5.5h1a.5.5 0 0 0 .5-.5V8a3 3 0 0 1 6 0v4.5a.5.5 0 0 0 .5.5h1a.5.5 0 0 0 .5-.5V8a5 5 0 0 0-5-5z"/>
                            <path d="M4.5 9a.5.5 0 0 0-.5.5v3a.5.5 0 0 0 .5.5h7a.5.5 0 0 0 .5-.5v-3a.5.5 0 0 0-.5-.5h-7z"/>
                        </svg>
                    </div>
                    <h3>Optional system audio</h3>
                    <p>Stream your PC audio (WASAPI loopback) along with the screen.</p>
                    <ul class="feature-list">
                        <li>Audio is optional (you can disable it)</li>
                        <li>Server-side mute control</li>
                        <li>Client-side local mute</li>
                        <li>Browser audio may require a click to start</li>
                    </ul>
                </div>

                <!-- Feature 3 -->
                <div class="feature-card animate-in" style="animation-delay: 0.3s">
                    <div class="feature-icon">
                        <svg width="30" height="30" fill="currentColor" viewBox="0 0 16 16">
                            <path d="M3 14s-1 0-1-1 1-4 6-4 6 3 6 4-1 1-1 1H3zm5-6a3 3 0 1 0 0-6 3 3 0 0 0 0 6z"/>
                        </svg>
                    </div>
                    <h3>Built-in Windows viewer</h3>
                    <p>Connect to a server URL in a native window (no browser required).</p>
                    <ul class="feature-list">
                        <li>Good for kiosks / second-PC viewing</li>
                        <li>Audio playback support</li>
                        <li>Simple, portable, fast startup</li>
                        <li>Works well on LAN / hotspot setups</li>
                    </ul>
                </div>

                <!-- Feature 4 -->
                <div class="feature-card animate-in" style="animation-delay: 0.4s">
                    <div class="feature-icon">
                        <svg width="30" height="30" fill="currentColor" viewBox="0 0 16 16">
                            <path d="M6 10.5a.5.5 0 0 1 .5-.5h3a.5.5 0 0 1 0 1h-3a.5.5 0 0 1-.5-.5zm-2-3a.5.5 0 0 1 .5-.5h7a.5.5 0 0 1 0 1h-7a.5.5 0 0 1-.5-.5zm-2-3a.5.5 0 0 1 .5-.5h11a.5.5 0 0 1 0 1h-11a.5.5 0 0 1-.5-.5z"/>
                        </svg>
                    </div>
                    <h3>GUI + CLI</h3>
                    <p>Run it with a double-click GUI, or automate via command line.</p>
                    <ul class="feature-list">
                        <li>GUI launcher for start/stop + quick actions</li>
                        <li>CLI modes: server / client / udp-server / udp-client</li>
                        <li>Stop and detect running servers</li>
                        <li>Built for Windows</li>
                    </ul>
                </div>
            </div>
        </div>
    </section>

    <!-- Getting Started Section -->
    <section id="getting-started" class="section" style="background: var(--light);">
        <div class="container">
            <div class="section-title animate-in">
                <h2>Getting Started</h2>
                <p>Quick setup guide for LANSCR</p>
            </div>
            
            <div class="steps">
                <div class="step animate-in" style="animation-delay: 0.1s">
                    <h3>Download (recommended)</h3>
                    <p>Most users just download and run the EXE.</p>
                    <div class="code-block">
                        # Download prebuilt EXE from GitHub Releases<br>
                        <?php echo h($github_releases_url); ?><br><br>
                        # Run (GUI)<br>
                        Double-click LANSCR.exe<br><br>
                        # Or run (CLI server)<br>
                        LANSCR.exe server 8000
                    </div>
                </div>
                
                <div class="step animate-in" style="animation-delay: 0.2s">
                    <h3>Start the Server</h3>
                    <p>Start sharing from your main PC</p>
                    <div class="code-block">
                        # Simple (recommended)<br>
                        LANSCR.exe server 8000<br><br>
                        # Optional (advanced)<br>
                        LANSCR.exe server 8080 30 85
                    </div>
                </div>
                
                <div class="step animate-in" style="animation-delay: 0.3s">
                    <h3>Connect Clients</h3>
                    <p>Connect using browser, built-in viewer, or command line</p>
                    <div class="code-block">
                        # Using built-in viewer<br>
                        LANSCR.exe client http://192.168.1.100:8080<br><br>
                        # In browser<br>
                        http://192.168.1.100:8080/<br><br>
                        # Stop server<br>
                        LANSCR.exe stop 8080
                    </div>
                </div>
            </div>
        </div>
    </section>

    <!-- Downloads Section -->
    <section id="downloads" class="section">
        <div class="container">
            <div class="section-title animate-in">
                <h2>Downloads</h2>
                <p>Choose a version, then download from GitHub Releases (recommended) or local hosting.</p>
            </div>
            
            <div class="download-section animate-in">
                <div class="download-card">
                    <h3>Latest: LANSCR <?php echo h($latest_release['tag']); ?></h3>
                    <p><?php echo h($latest_release['channel']); ?> Release</p>
                    
                    <div class="download-info">
                        <div class="info-item">
                            <span class="value"><?php echo h($latest_release['tag']); ?></span>
                            <span class="label">Tag</span>
                        </div>
                        <div class="info-item">
                            <span class="value"><?php echo date('M j, Y', strtotime($latest_release['release_date'])); ?></span>
                            <span class="label">Release Date</span>
                        </div>
                        <div class="info-item">
                            <span class="value">Windows</span>
                            <span class="label">Platform</span>
                        </div>
                        <div class="info-item">
                            <span class="value">LAN</span>
                            <span class="label">Use</span>
                        </div>
                    </div>
                    
                    <div class="download-options">
                        <a href="<?php echo h($github_releases_url); ?>" class="btn btn-primary">
                            <svg width="20" height="20" fill="currentColor" viewBox="0 0 16 16">
                                <path d="M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5z"/>
                                <path d="M7.646 11.854a.5.5 0 0 0 .708 0l3-3a.5.5 0 0 0-.708-.708L8.5 10.293V1.5a.5.5 0 0 0-1 0v8.793L5.354 8.146a.5.5 0 1 0-.708.708l3 3z"/>
                            </svg>
                            Open GitHub Releases
                        </a>
                        <a href="<?php echo h($github_repo_url); ?>" class="btn btn-secondary">
                            <svg width="20" height="20" fill="currentColor" viewBox="0 0 16 16">
                                <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.012 8.012 0 0 0 16 8c0-4.42-3.58-8-8-8z"/>
                            </svg>
                            View on GitHub
                        </a>
                    </div>
                </div>

                <div class="version-tabs" aria-label="Version selector">
                    <?php foreach ($releases as $idx => $rel):
                        $rid = release_id($rel['tag']);
                        $active = $idx === 0;
                    ?>
                        <button class="version-tab <?php echo $active ? 'active' : ''; ?>" onclick="showRelease('<?php echo h($rid); ?>', this)">
                            <?php echo h($rel['tag']); ?>
                        </button>
                    <?php endforeach; ?>
                </div>

                <?php foreach ($releases as $idx => $rel):
                    $rid = release_id($rel['tag']);
                    $active = $idx === 0;
                ?>
                    <div id="<?php echo h($rid); ?>" class="version-content <?php echo $active ? 'active' : ''; ?>">
                        <h4><?php echo h($rel['channel']); ?> • <?php echo h($rel['tag']); ?></h4>
                        <p style="color: var(--gray); margin-bottom: 1rem;">Released <?php echo date('M j, Y', strtotime($rel['release_date'])); ?></p>

                        <h5 style="margin: 1.25rem 0 0.5rem;">Downloads</h5>
                        <div class="download-options" style="justify-content: flex-start;">
                            <?php foreach ($rel['assets'] as $asset): ?>
                                <a href="<?php echo h($asset['href']); ?>" class="btn btn-primary">
                                    <?php echo h($asset['label']); ?>
                                </a>
                            <?php endforeach; ?>
                        </div>

                        <h5 style="margin: 1.25rem 0 0.5rem;">Highlights</h5>
                        <ul class="feature-list">
                            <?php foreach ($rel['highlights'] as $hl): ?>
                                <li><?php echo h($hl); ?></li>
                            <?php endforeach; ?>
                        </ul>

                        <p style="color: var(--gray); margin-top: 1rem;">
                            Tip: For safest downloads, prefer the official GitHub release page.
                        </p>
                    </div>
                <?php endforeach; ?>
            </div>
        </div>
    </section>

    <!-- Documentation Section -->
    <section id="documentation" class="section" style="background: var(--light);">
        <div class="container">
            <div class="section-title animate-in">
                <h2>Documentation</h2>
                <p>Simple guide for normal users, with advanced details available when needed.</p>
            </div>
            
            <div class="features-grid">
                <div class="feature-card">
                    <h3>How to use (most people)</h3>
                    <p>Start the server, then open it from another device on the same network.</p>
                    <div class="code-block" style="background: var(--gray-light); color: var(--dark);">
                        <strong>1) Start server</strong><br>
                        LANSCR.exe server 8000<br><br>
                        <strong>2) On another device</strong><br>
                        Open: http://&lt;your-pc-ip&gt;:8000/<br><br>
                        <strong>3) Optional (Windows viewer)</strong><br>
                        LANSCR.exe client http://&lt;your-pc-ip&gt;:8000/
                    </div>
                </div>
                
                <div class="feature-card">
                    <h3>Full CLI help (advanced)</h3>
                    <p>Not required for normal use. Expand only if you need all options.</p>
                    <details style="margin-top: 0.75rem;">
                        <summary style="cursor: pointer; font-weight: 600;">Show full help output</summary>
                        <pre class="code-block" style="margin-top: 0.75rem; white-space: pre-wrap; background: var(--gray-light); color: var(--dark);"><?php echo h($cli_help_text); ?></pre>
                    </details>
                </div>
                
                <div class="feature-card">
                    <h3>What this app is</h3>
                    <p>LANSCR is a Windows LAN screen-share tool. It streams:</p>
                    <ul class="feature-list">
                        <li>Video: MJPEG over HTTP</li>
                        <li>Audio (optional): WAV (PCM16) over HTTP</li>
                        <li>Viewer: browser or native Windows client</li>
                    </ul>
                </div>
                
                <div class="feature-card">
                    <h3>Safety & network notes</h3>
                    <p>Designed for trusted LAN / hotspot use.</p>
                    <ul class="feature-list">
                        <li>No login/password by default</li>
                        <li>No encryption (HTTP, not HTTPS)</li>
                        <li>Use Windows Firewall to limit access</li>
                        <li>Don’t expose it to the public internet</li>
                    </ul>
                </div>

                <div class="feature-card">
                    <h3>FAQ (quick fixes)</h3>
                    <ul class="feature-list">
                        <li>Find your PC IP: run <code>ipconfig</code> and use the IPv4 address</li>
                        <li>Audio in browser may require a click (autoplay rules)</li>
                        <li>If another device can’t connect: allow the port in Windows Firewall</li>
                        <li>“Unknown Publisher” is normal for unsigned EXEs (code signing required)</li>
                    </ul>
                </div>
            </div>

            <div style="margin-top: 2rem; text-align: center;">
                <a href="<?php echo h($github_repo_url); ?>" class="btn btn-secondary">GitHub Repository</a>
                <a href="<?php echo h($github_releases_url); ?>" class="btn btn-primary">GitHub Releases</a>
            </div>
        </div>
    </section>

    <!-- Contribute Section -->
    <section id="contribute" class="section">
        <div class="container">
            <div class="section-title animate-in">
                <h2>Contribute</h2>
                <p>Help improve LANSCR — issues, PRs, ideas, and testing are welcome.</p>
            </div>

            <div class="features-grid">
                <div class="feature-card animate-in" style="animation-delay: 0.05s">
                    <h3>GitHub repository</h3>
                    <p>Star the repo, follow development, and see the full source code.</p>
                    <div style="margin-top: 1rem; display: flex; gap: 0.75rem; flex-wrap: wrap;">
                        <a class="btn btn-primary" href="<?php echo h($github_repo_url); ?>">View Repo</a>
                        <a class="btn btn-secondary" href="<?php echo h($github_owner_url); ?>">Developer Profile</a>
                    </div>
                </div>

                <div class="feature-card animate-in" style="animation-delay: 0.10s">
                    <h3>Report issues</h3>
                    <p>Found a bug or have a feature request? Open an issue with steps + screenshots.</p>
                    <div style="margin-top: 1rem; display: flex; gap: 0.75rem; flex-wrap: wrap;">
                        <a class="btn btn-primary" href="<?php echo h($github_issues_url); ?>">Open Issue</a>
                        <a class="btn btn-secondary" href="<?php echo h($github_releases_url); ?>">Check Releases</a>
                    </div>
                </div>

                <div class="feature-card animate-in" style="animation-delay: 0.15s">
                    <h3>Pull requests</h3>
                    <p>Fixes and improvements are welcome. Keep changes focused and explain the why.</p>
                    <div style="margin-top: 1rem; display: flex; gap: 0.75rem; flex-wrap: wrap;">
                        <a class="btn btn-primary" href="<?php echo h($github_pulls_url); ?>">View PRs</a>
                        <a class="btn btn-secondary" href="<?php echo h($github_readme_url); ?>">Read Docs</a>
                    </div>
                </div>
            </div>
        </div>
    </section>

    <!-- Development Section -->
    <section id="development" class="section" style="background: var(--light);">
        <div class="container">
            <div class="section-title animate-in">
                <h2>Development</h2>
                <p>Build from source on Windows, or download the prebuilt EXE.</p>
            </div>

            <div class="features-grid">
                <div class="feature-card animate-in" style="animation-delay: 0.05s">
                    <h3>Quick build (recommended)</h3>
                    <p>Use the one-file automation script included in the project.</p>
                    <div class="code-block" style="background: var(--gray-light); color: var(--dark);">
                        Double-click: Setup.bat<br>
                        (It can download the EXE or install build tools and compile.)
                    </div>
                </div>

                <div class="feature-card animate-in" style="animation-delay: 0.10s">
                    <h3>Toolchains</h3>
                    <ul class="feature-list">
                        <li><strong>MSVC (Visual Studio Build Tools)</strong> — best compatibility</li>
                        <li><strong>MSYS2 UCRT64 (MinGW-w64)</strong> — smaller install, GCC-based</li>
                        <li>Windows SDK headers are required for audio/screen APIs</li>
                    </ul>
                </div>

                <div class="feature-card animate-in" style="animation-delay: 0.15s">
                    <h3>For contributors</h3>
                    <ul class="feature-list">
                        <li>Keep PRs small and focused</li>
                        <li>Test on a real LAN (Wi‑Fi/hotspot)</li>
                        <li>Include screenshots for UI changes</li>
                    </ul>
                </div>
            </div>
        </div>
    </section>

    <!-- Privacy Section -->
    <section id="privacy" class="section">
        <div class="container">
            <div class="section-title animate-in">
                <h2>Privacy</h2>
                <p>Designed for trusted local networks. Be careful if you expose it beyond LAN.</p>
            </div>

            <div class="features-grid">
                <div class="feature-card animate-in" style="animation-delay: 0.05s">
                    <h3>Local-first</h3>
                    <ul class="feature-list">
                        <li>LANSCR streams over your LAN / hotspot</li>
                        <li>No built-in cloud accounts</li>
                        <li>No analytics or tracking on this page</li>
                    </ul>
                </div>

                <div class="feature-card animate-in" style="animation-delay: 0.10s">
                    <h3>Security notes</h3>
                    <ul class="feature-list">
                        <li>HTTP streaming (not HTTPS)</li>
                        <li>Use Windows Firewall to restrict who can connect</li>
                        <li>Do not port-forward to the public internet</li>
                    </ul>
                </div>

                <div class="feature-card animate-in" style="animation-delay: 0.15s">
                    <h3>Private mode</h3>
                    <ul class="feature-list">
                        <li>Optional username/password protection</li>
                        <li>Best for shared networks (labs / offices)</li>
                        <li>Still recommended to keep it on LAN</li>
                    </ul>
                </div>
            </div>
        </div>
    </section>

    <!-- Support Section -->
    <section id="support" class="section" style="background: var(--light);">
        <div class="container">
            <div class="section-title animate-in">
                <h2>Support & Contact</h2>
                <p>Need help, want a feature, or found a bug? Use GitHub for fastest support.</p>
            </div>

            <div class="features-grid">
                <div class="feature-card animate-in" style="animation-delay: 0.05s">
                    <h3>Get support</h3>
                    <p>Open an issue with your Windows version, steps, and screenshots.</p>
                    <div style="margin-top: 1rem; display: flex; gap: 0.75rem; flex-wrap: wrap;">
                        <a class="btn btn-primary" href="<?php echo h($github_issues_url); ?>">Issue Tracker</a>
                        <a class="btn btn-secondary" href="<?php echo h($github_repo_url); ?>">Repository</a>
                    </div>
                </div>

                <div class="feature-card animate-in" style="animation-delay: 0.10s">
                    <h3>Download</h3>
                    <p>For normal users, downloading the portable EXE is the easiest option.</p>
                    <div style="margin-top: 1rem; display: flex; gap: 0.75rem; flex-wrap: wrap;">
                        <a class="btn btn-primary" href="<?php echo h($github_release_asset_url); ?>">Download LANSCR.exe</a>
                        <a class="btn btn-secondary" href="<?php echo h($github_releases_url); ?>">All Releases</a>
                    </div>
                </div>

                <div class="feature-card animate-in" style="animation-delay: 0.15s">
                    <h3>Developer</h3>
                    <p>Follow the project and contribute ideas.</p>
                    <div style="margin-top: 1rem; display: flex; gap: 0.75rem; flex-wrap: wrap;">
                        <a class="btn btn-secondary" href="<?php echo h($github_owner_url); ?>">GitHub Profile</a>
                        <a class="btn btn-secondary" href="<?php echo h($github_pulls_url); ?>">Pull Requests</a>
                    </div>
                </div>
            </div>
        </div>
    </section>

    <!-- License Section -->
    <section id="license" class="section">
        <div class="container">
            <div class="section-title animate-in">
                <h2>License</h2>
                <p>Open-source licensing information.</p>
            </div>

            <div class="features-grid">
                <div class="feature-card animate-in" style="animation-delay: 0.05s">
                    <h3>Source license</h3>
                    <p>The license is published in the GitHub repository (if present).</p>
                    <div style="margin-top: 1rem; display: flex; gap: 0.75rem; flex-wrap: wrap;">
                        <a class="btn btn-primary" href="<?php echo h($github_license_url); ?>">View LICENSE</a>
                        <a class="btn btn-secondary" href="<?php echo h($github_repo_url); ?>">Repo Home</a>
                    </div>
                </div>

                <div class="feature-card animate-in" style="animation-delay: 0.10s">
                    <h3>Third‑party</h3>
                    <p>LANSCR uses Windows APIs (WinHTTP, WASAPI, WIC) and ships as a portable EXE.</p>
                    <ul class="feature-list">
                        <li>No external runtime installer required</li>
                        <li>Unsigned builds may show “Unknown Publisher”</li>
                    </ul>
                </div>
            </div>
        </div>
    </section>

    <!-- Footer -->
    <footer>
        <div class="container">
            <div class="footer-content">
                <div class="footer-section">
                    <h3>LANSCR</h3>
                    <p>High-performance local network screen sharing with audio streaming. Open source Windows application.</p>
                </div>
                
                <div class="footer-section">
                    <h3>Quick Links</h3>
                    <ul class="footer-links">
                        <li><a href="#home">Home</a></li>
                        <li><a href="#features">Features</a></li>
                        <li><a href="#getting-started">Getting Started</a></li>
                        <li><a href="#downloads">Downloads</a></li>
                        <li><a href="#documentation">Documentation</a></li>
                        <li><a href="#contribute">Contribute</a></li>
                        <li><a href="#development">Development</a></li>
                        <li><a href="#privacy">Privacy</a></li>
                        <li><a href="#support">Support</a></li>
                        <li><a href="#license">License</a></li>
                    </ul>
                </div>
                
                <div class="footer-section">
                    <h3>Resources</h3>
                    <ul class="footer-links">
                        <li><a href="<?php echo h($github_repo_url); ?>">GitHub Repository</a></li>
                        <li><a href="<?php echo h($github_owner_url); ?>">Developer Profile</a></li>
                        <li><a href="<?php echo h($github_issues_url); ?>">Issue Tracker</a></li>
                        <li><a href="<?php echo h($github_pulls_url); ?>">Pull Requests</a></li>
                        <li><a href="<?php echo h($github_releases_url); ?>">Releases</a></li>
                        <li><a href="<?php echo h($github_license_url); ?>">LICENSE</a></li>
                    </ul>
                </div>
                
                <div class="footer-section">
                    <h3>Technical Info</h3>
                    <ul class="footer-links">
                        <li><a href="README.md">README</a></li>
                        <li><a href="features.txt">Features</a></li>
                        <li><a href="instructions.txt">Build/Run Notes</a></li>
                        <li><a href="<?php echo h($github_repo_url); ?>">Source (GitHub)</a></li>
                    </ul>
                </div>
            </div>
            
            <div class="copyright">
                <p>&copy; <?php echo date('Y'); ?> LANSCR Project. All rights reserved.</p>
                <p>Project links and source code: <a href="<?php echo h($github_repo_url); ?>" style="color: var(--gray-light);">GitHub</a></p>
            </div>
        </div>
    </footer>

        </main>
    </div>

    <script>
        // Sidebar (mobile drawer)
        const sidebar = document.getElementById('sidebar');
        function setSidebarOpen(open) {
            document.body.classList.toggle('sidebar-open', open);
            document.querySelectorAll('[data-action="toggleSidebar"]').forEach(btn => {
                btn.setAttribute('aria-expanded', open ? 'true' : 'false');
            });
        }
        document.addEventListener('click', (e) => {
            const actionEl = e.target.closest('[data-action]');
            if (!actionEl) return;
            const action = actionEl.getAttribute('data-action');
            if (action === 'toggleSidebar') {
                setSidebarOpen(!document.body.classList.contains('sidebar-open'));
            }
            if (action === 'closeSidebar') {
                setSidebarOpen(false);
            }
        });
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') setSidebarOpen(false);
        });

        // Release tabs functionality
        function showRelease(releaseId, btn) {
            document.querySelectorAll('.version-content').forEach(content => {
                content.classList.remove('active');
            });
            document.querySelectorAll('.version-tab').forEach(tab => {
                tab.classList.remove('active');
            });
            const target = document.getElementById(releaseId);
            if (target) target.classList.add('active');
            if (btn) btn.classList.add('active');
        }
        
        // Smooth scrolling for anchor links
        document.querySelectorAll('a[href^="#"]').forEach(anchor => {
            anchor.addEventListener('click', function (e) {
                e.preventDefault();
                const targetId = this.getAttribute('href');
                if(targetId === '#') return;

                // Close drawer on navigation (mobile)
                if (document.body.classList.contains('sidebar-open')) {
                    setSidebarOpen(false);
                }
                
                const targetElement = document.querySelector(targetId);
                if(targetElement) {
                    const mobileTopbar = document.querySelector('.mobile-topbar');
                    const offset = (mobileTopbar && getComputedStyle(mobileTopbar).display !== 'none') ? mobileTopbar.offsetHeight : 24;
                    window.scrollTo({
                        top: targetElement.offsetTop - offset,
                        behavior: 'smooth'
                    });
                }
            });
        });

        // Active section highlighting for sidebar
        const sectionIds = Array.from(document.querySelectorAll('section[id]')).map(s => s.id);
        const navLinksById = new Map();
        document.querySelectorAll('.nav-item[href^="#"]').forEach(a => {
            const id = a.getAttribute('href').slice(1);
            if (id) navLinksById.set(id, a);
        });

        const activeObserver = new IntersectionObserver((entries) => {
            const visible = entries
                .filter(en => en.isIntersecting)
                .sort((a, b) => (b.intersectionRatio - a.intersectionRatio));
            if (!visible.length) return;
            const id = visible[0].target.id;
            navLinksById.forEach((a) => a.removeAttribute('aria-current'));
            const active = navLinksById.get(id);
            if (active) active.setAttribute('aria-current', 'page');
        }, { rootMargin: '-20% 0px -70% 0px', threshold: [0.05, 0.12, 0.2] });

        sectionIds.forEach(id => {
            const el = document.getElementById(id);
            if (el) activeObserver.observe(el);
        });
        
        // Animation on scroll
        const observerOptions = {
            threshold: 0.1,
            rootMargin: '0px 0px -50px 0px'
        };
        
        const observer = new IntersectionObserver((entries) => {
            entries.forEach(entry => {
                if(entry.isIntersecting) {
                    entry.target.classList.add('animate-in');
                }
            });
        }, observerOptions);
        
        // Observe all elements with animate-in class
        document.querySelectorAll('.animate-in').forEach(el => {
            observer.observe(el);
        });
    </script>
</body>
</html>