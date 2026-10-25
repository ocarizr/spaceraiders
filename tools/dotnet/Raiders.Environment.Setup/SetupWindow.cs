using System.Drawing;
using Raiders.Diagnostics;

namespace Raiders.Environment.Setup;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        ApplicationConfiguration.Initialize();
        var localRoot = Path.Combine(
            System.Environment.GetFolderPath(System.Environment.SpecialFolder.LocalApplicationData),
            "Raiders", "Setup");
        using var sink = OpenLog(localRoot);
        var session = new TelemetrySession("setup." + Guid.NewGuid().ToString("N"));
        var log = new Log(session, sink);
        try
        {
            using var model =
                new ProfileViewModel(new WindowsSetupBackend(localRoot, session, log), log);
            using var window = new SetupWindow(model);
            log.Write(LogLevel.Info, "setup", () => "Environment setup started.");

            Application.Run(window);
            return 0;
        }
        catch (Exception)
        {
            log.Write(LogLevel.Error, "setup", () => "Environment setup could not start.");

            MessageBox.Show("Environment setup could not start. Check that the application was " +
                                "extracted completely.",
                            "Raiders Engine setup", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }

    private static TextWriter OpenLog(string root)
    {
        try
        {
            Directory.CreateDirectory(Path.Combine(root, "logs"));
            return new StreamWriter(
                Path.Combine(root, "logs", Guid.NewGuid().ToString("N") + ".jsonl"));
        }
        catch (IOException)
        {
            return TextWriter.Null;
        }
        catch (UnauthorizedAccessException)
        {
            return TextWriter.Null;
        }
    }
}

public sealed class SetupWindow : Form
{
    private readonly ProfileViewModel model;
    private readonly ListView profiles =
        new() { Name = "Profiles",     View = View.Details,
                CheckBoxes = true,     FullRowSelect = true,
                HideSelection = false, Dock = DockStyle.Fill,
                MultiSelect = false,   AccessibleName = "Environments to prepare" };
    private readonly DataGridView checks =
        new() { Name = "Checks",
                Dock = DockStyle.Fill,
                ReadOnly = true,
                AllowUserToAddRows = false,
                AllowUserToDeleteRows = false,
                AllowUserToResizeRows = false,
                RowHeadersVisible = false,
                MultiSelect = false,
                SelectionMode = DataGridViewSelectionMode.FullRowSelect,
                AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill,
                BackgroundColor = Color.White,
                BorderStyle = BorderStyle.None,
                AccessibleName = "Environment checks and available actions" };
    private readonly TextBox details = new() { Name = "Evidence",
                                               Dock = DockStyle.Fill,
                                               Multiline = true,
                                               ReadOnly = true,
                                               ScrollBars = ScrollBars.Vertical,
                                               BackColor = Color.White,
                                               BorderStyle = BorderStyle.FixedSingle,
                                               AccessibleName = "Selected component evidence" };
    private readonly Label status = new() { Name = "Status",
                                            Dock = DockStyle.Fill,
                                            AutoSize = true,
                                            MaximumSize = new Size(1100, 0),
                                            Padding = new Padding(0, 8, 0, 8),
                                            AccessibleName = "Operation status" };
    private readonly ProgressBar progress =
        new() { Name = "Progress", Dock = DockStyle.Top, Height = 8,
                AccessibleName = "Preparation progress" };
    private readonly Button scan =
        new() { Name = "Scan", Text = "&Scan selected", AutoSize = true,
                MinimumSize = new Size(140, 38), AccessibleName = "Scan selected environments" };
    private readonly Button prepare =
        new() { Name = "Prepare",
                Text = "&Prepare available actions",
                AutoSize = true,
                MinimumSize = new Size(205, 38),
                BackColor = Color.FromArgb(29, 78, 216),
                ForeColor = Color.White,
                FlatStyle = FlatStyle.Flat,
                AccessibleName = "Prepare reviewed available actions" };
    private readonly Button cancel =
        new() { Name = "Cancel", Text = "&Cancel", AutoSize = true, MinimumSize = new Size(100, 38),
                AccessibleName = "Cancel current operation" };
    private SetupAction[] rows = [];
    private bool closing;

    public SetupWindow(ProfileViewModel model)
    {
        this.model = model;
        Text = "Raiders Engine — Environment setup";
        Name = "RaidersSetup";
        AccessibleName = "Raiders Engine environment setup";
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(1200, 760);
        MinimumSize = new Size(1020, 700);
        AutoScaleMode = AutoScaleMode.Dpi;
        AutoScaleDimensions = new SizeF(96, 96);
        Font = new Font("Segoe UI", 10);
        BackColor = Color.FromArgb(244, 246, 249);
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 4,
                                          Padding = new Padding(24) };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 94));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 60));
        var header = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2, ColumnCount = 1 };
        header.Controls.Add(new Label { Text = "Environment setup", AutoSize = true,
                                        Font = new Font("Segoe UI", 24, FontStyle.Bold),
                                        ForeColor = Color.FromArgb(18, 35, 62) });
        header.Controls.Add(new Label { Text = "RAIDERS ENGINE  /  Choose your environments. " +
                                               "Review the checks. Prepare what you need.",
                                        AutoSize = true, ForeColor = Color.FromArgb(70, 85, 105) });
        root.Controls.Add(header, 0, 0);
        var body = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1 };
        body.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 335));
        body.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        var left = Section("1. Select environments", profiles);
        left.Padding = new Padding(0, 0, 18, 0);
        profiles.Columns.Add("Environment", 205);
        profiles.Columns.Add("State", 105);
        foreach (var profile in model.Catalog.Profiles)
        {
            var item = new ListViewItem(profile.Title) { Tag = profile.Id };
            item.SubItems.Add("Not checked");
            profiles.Items.Add(item);
        }

        var right = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2, ColumnCount = 1 };
        right.RowStyles.Add(new RowStyle(SizeType.Percent, 55));
        right.RowStyles.Add(new RowStyle(SizeType.Percent, 45));
        checks.Columns.Add("Component", "Component");
        checks.Columns.Add("State", "State");
        checks.Columns.Add("Action", "Action");
        checks.Columns[0].FillWeight = 50;
        checks.Columns[1].FillWeight = 24;
        checks.Columns[2].FillWeight = 26;
        checks.AutoSizeRowsMode = DataGridViewAutoSizeRowsMode.AllCells;
        checks.ColumnHeadersHeight = 36;
        checks.ColumnHeadersDefaultCellStyle.Padding = new Padding(4);
        checks.DefaultCellStyle.WrapMode = DataGridViewTriState.True;
        checks.DefaultCellStyle.Padding = new Padding(4, 6, 4, 6);
        right.Controls.Add(Section("2. Review checks and actions", checks), 0, 0);
        right.Controls.Add(Section("Component details", details), 0, 1);
        body.Controls.Add(left, 0, 0);
        body.Controls.Add(right, 1, 0);
        root.Controls.Add(body, 0, 1);
        root.Controls.Add(status, 0, 2);
        var footer = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2, ColumnCount = 1 };
        footer.RowStyles.Add(new RowStyle(SizeType.Absolute, 10));
        footer.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        footer.Controls.Add(progress, 0, 0);
        var buttons =
            new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft,
                                  WrapContents = false };
        buttons.Controls.Add(cancel);
        buttons.Controls.Add(prepare);
        buttons.Controls.Add(scan);
        footer.Controls.Add(buttons, 0, 1);
        root.Controls.Add(footer, 0, 3);
        Controls.Add(root);
        profiles.ItemCheck += (_, value) => model.Select((string)profiles.Items[value.Index].Tag!,
                                                         value.NewValue == CheckState.Checked);

        checks.SelectionChanged += (_, _) => ShowEvidence();

        scan.Click += async (_, _) => await model.ScanAsync();

        prepare.Click += async (_, _) =>
        {
            var actions = model.Actions.Where(action => action.Action is "Install" or "Repair")
                              .Select(action => action.Action + " " + action.Requirement + "\n" +
                                                action.Details);
            var answer = MessageBox.Show(
                this,
                "Prepare these available actions?\n\n" + string.Join("\n", actions) +
                    "\n\nComponents marked unavailable will remain unresolved.",
                "Review environment changes", MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            if (answer == DialogResult.Yes)
            {
                await model.PrepareAsync();
            }
        };

        cancel.Click += (_, _) => model.Cancel();

        model.Changed += OnChanged;
        FormClosing += (_, args) =>
        {
            if (model.IsBusy)
            {
                closing = true;
                args.Cancel = true;
                model.Cancel();
            }
        };

        Disposed += (_, _) => model.Changed -= OnChanged;

        RefreshState();
    }

    private static TableLayoutPanel Section(string title, Control content)
    {
        var panel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2,
                                           Margin = new Padding(0, 0, 0, 12) };
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 32));
        panel.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        panel.Controls.Add(new Label { Text = title, Dock = DockStyle.Fill, AutoSize = true,
                                       ForeColor = Color.FromArgb(35, 50, 75) },
                           0, 0);
        panel.Controls.Add(content, 0, 1);
        return panel;
    }

    private void OnChanged(object? sender, EventArgs args)
    {
        if (IsDisposed || Disposing)
        {
            return;
        }

        if (InvokeRequired)
        {
            BeginInvoke(() => OnChanged(sender, args));

            return;
        }

        RefreshState();
        if (closing && !model.IsBusy)
        {
            BeginInvoke(Close);
        }
    }

    private void RefreshState()
    {
        profiles.Enabled = !model.IsBusy;
        scan.Enabled = model.CanScan;
        prepare.Enabled = model.CanPrepare;
        cancel.Enabled = model.IsBusy;
        status.Text = model.Message;
        var update = model.Progress;
        progress.Style =
            model.IsBusy && update is null ? ProgressBarStyle.Marquee : ProgressBarStyle.Continuous;
        progress.Value = update is { TotalSteps : > 0 }
                             ? Math.Clamp(update.CompletedSteps * 100 / update.TotalSteps, 0, 100)
                             : 0;
        if (model.IsBusy && update is not null)
        {
            status.Text =
                $"{update.Stage}: {update.RequirementId ?? "selected environments"} — {update.CompletedSteps}/{update.TotalSteps} steps";
        }

        foreach (ListViewItem item in profiles.Items)
        {
            item.SubItems[1].Text = model.ProfileState((string)item.Tag!);
        }

        var updated = model.Actions.ToArray();
        if (!rows.SequenceEqual(updated))
        {
            rows = updated;
            checks.Rows.Clear();
            foreach (var row in rows)
            {
                checks.Rows.Add(row.Requirement, row.State, row.Action);
            }
        }

        ShowEvidence();
    }

    private void ShowEvidence()
    {
        if (checks.CurrentRow is null || checks.CurrentRow.Index >= rows.Length ||
            model.Health is null)
        {
            details.Text = "Select environments on the left and scan to see versions, locations " +
                           "and available actions.";
            return;
        }

        var row = rows[checks.CurrentRow.Index];
        var profile = model.Catalog.Profiles.Single(item => item.Id == row.Profile);
        var policy = profile.Requirements.Single(item => item.Id == row.Requirement);
        var assessment = model.Health.Profiles.Single(item => item.Id == row.Profile)
                             .Requirements.Single(item => item.Id == row.Requirement);
        var expected = policy.Constraint.Value ??
                       string.Join(", ", policy.Constraint.Values ?? [policy.Constraint.Reference ??
                                                                      "Approved configuration"]);
        var evidence = assessment.Evidence.Select(
            item =>
                $"Detected version: {item.ObservedVersion ?? "Not determined"}\r\nArchitecture: {item.ObservedArchitecture}\r\nLocation: {item.Location.Value ?? "Not available"}\r\nChecked: {item.ObservedAt}");
        var previous = model.Preparation?.Steps.FirstOrDefault(
            item => item.ProfileId == row.Profile && item.RequirementId == row.Requirement);
        details.Text =
            $"{profile.Title}\r\n{row.Requirement} ({(policy.Required ? "required" : "optional")})\r\n\r\nState: {row.State}\r\nAction: {row.Action}\r\nRequired version / definition: {expected}\r\n{row.Details}\r\n\r\n" +
            string.Join("\r\n\r\n", evidence) +
            (previous is null
                 ? ""
                 : $"\r\n\r\nLast preparation: {previous.Status}. {ProfileViewModel.Explain(previous.Reason.ToString())}");
    }
}
