function chess_vs_engine()

    clc; close all;
    cfg = start_menu();
    if isempty(cfg), return; end

    this_dir = fileparts(mfilename('fullpath'));
    if isempty(this_dir), this_dir = pwd; end
    eng_paths = {
        fullfile(this_dir, '..', 'engine_source'), ...
        fullfile(this_dir, 'engine_source')
    };
    for ep = 1:numel(eng_paths)
        if exist(eng_paths{ep}, 'dir') == 7
            addpath(eng_paths{ep});
        end
    end

    use_mex = (exist('engine_search_mex','file') == 3 || exist('engine_search','file') == 3);

    if isfield(cfg,'mode') && strcmp(cfg.mode,'pgn')
        pgn_str = pgn_dialog_get_string();
        if isempty(pgn_str), return; end
        run_pgn_analysis(pgn_str, use_mex);
        return;
    end

    s = struct();
    s.board=init_board(); s.side=1; s.castling=[1 1 1 1]; s.en_passant=[0 0];
    s.selected=[0 0]; s.legal_sel=zeros(0,4); s.game_over=false;
    s.human=cfg.human_side; s.elo=cfg.elo; s.style=cfg.style_vec;
    s.style_name=cfg.style_name; s.flip=(cfg.human_side==-1);
    s.sq_size=64; s.pieces=build_all_sprites(s.sq_size);
    s.use_mex=use_mex;
    s.book=build_book(); s.ply=0; s.history=[];
    s.game_result='*';
    s.clock_enabled=cfg.clock_base>0; s.clock_w=cfg.clock_base;
    s.clock_b=cfg.clock_base; s.clock_inc=cfg.clock_inc; s.last_tick=tic;

    s.engine_name = 'MEX Engine v11';

    fprintf('Engine: %s | ELO: %d | side: %s | style: %s | clock: %s\n', ...
        s.engine_name, s.elo, ternary(s.human==1,'White','Black'), ...
        s.style_name, cfg.clock_label);

    fig = figure('Name','Chess vs Engine','NumberTitle','off', ...
        'MenuBar','none','ToolBar','none','Position',[150 20 730 930], ...
        'Color',[0.10 0.10 0.12],'Resize','off', ...
        'WindowButtonDownFcn',@click_cb,'CloseRequestFcn',@on_close);

    ax = axes('Parent',fig,'Units','pixels','Position',[30 290 560 560], ...
        'XTick',[],'YTick',[],'Box','on','LineWidth',1, ...
        'DataAspectRatio',[1 1 1]);
    hold(ax,'on');

    eval_ax = axes('Parent',fig,'Units','pixels','Position',[30 258 560 24], ...
        'XTick',[],'YTick',[],'Box','on','LineWidth',1, ...
        'XLim',[0 1],'YLim',[0 1],'Color',[0.15 0.15 0.15]);
    hold(eval_ax,'on');

    uicontrol('Style','text','Parent',fig,'Units','pixels', ...
        'Position',[608 800 115 25],'String','ELO:','FontSize',11, ...
        'BackgroundColor',[0.10 0.10 0.12],'ForegroundColor',[1 1 1], ...
        'HorizontalAlignment','left');
    elos=[800 1000 1200 1400 1600 1800 2000 2200 2400 2600 2800 3000 3200];
    val=find(elos==s.elo,1); if isempty(val), val=6; end
    uicontrol('Style','popupmenu','Parent',fig,'Units','pixels', ...
        'Position',[608 770 115 25],'String',cellstr(num2str(elos(:))), ...
        'Value',val,'FontSize',11,'Callback',@elo_changed);

    uicontrol('Style','text','Parent',fig,'Units','pixels', ...
        'Position',[608 735 115 22],'String','Style:','FontSize',11, ...
        'BackgroundColor',[0.10 0.10 0.12],'ForegroundColor',[1 1 1], ...
        'HorizontalAlignment','left');
    uicontrol('Style','text','Parent',fig,'Units','pixels', ...
        'Position',[608 710 115 25],'String',s.style_name,'FontSize',10, ...
        'BackgroundColor',[0.10 0.10 0.12],'ForegroundColor',[0.8 0.8 1], ...
        'HorizontalAlignment','left');

    uicontrol('Style','text','Parent',fig,'Units','pixels', ...
        'Position',[608 680 115 22],'String','Engine:','FontSize',11, ...
        'BackgroundColor',[0.10 0.10 0.12],'ForegroundColor',[1 1 1], ...
        'HorizontalAlignment','left');
    uicontrol('Style','text','Parent',fig,'Units','pixels', ...
        'Position',[608 655 115 25],'String',s.engine_name,'FontSize',10, ...
        'BackgroundColor',[0.10 0.10 0.12],'ForegroundColor',[0.5 1 0.5], ...
        'HorizontalAlignment','left');

    black_clock_ui = uicontrol('Style','text','Parent',fig,'Units','pixels', ...
        'Position',[600 620 130 28],'String','Black --:--','FontSize',13, ...
        'FontWeight','bold','BackgroundColor',[0.10 0.10 0.12], ...
        'ForegroundColor',[1 1 1],'HorizontalAlignment','left');
    white_clock_ui = uicontrol('Style','text','Parent',fig,'Units','pixels', ...
        'Position',[600 585 130 28],'String','White --:--','FontSize',13, ...
        'FontWeight','bold','BackgroundColor',[0.10 0.10 0.12], ...
        'ForegroundColor',[1 1 1],'HorizontalAlignment','left');

    uicontrol('Style','text','Parent',fig,'Units','pixels', ...
        'Position',[30 215 660 30],'String','','FontSize',13, ...
        'FontWeight','bold','BackgroundColor',[0.10 0.10 0.12], ...
        'ForegroundColor',[0.85 0.85 0.85],'HorizontalAlignment','left', ...
        'Tag','analysis');
    analysis_box = findobj(fig,'Tag','analysis');

    uicontrol('Style','text','Parent',fig,'Units','pixels', ...
        'Position',[30 188 660 25],'String','','FontSize',11, ...
        'BackgroundColor',[0.10 0.10 0.12],'ForegroundColor',[0.75 0.75 0.75], ...
        'HorizontalAlignment','left','Tag','subinfo');
    subinfo_box = findobj(fig,'Tag','subinfo');

    status = uicontrol('Style','text','Parent',fig,'Units','pixels', ...
        'Position',[30 155 660 30],'String','','FontSize',13, ...
        'BackgroundColor',[0.10 0.10 0.12],'ForegroundColor',[1 1 1]);

    uicontrol('Style','pushbutton','Parent',fig,'Units','pixels', ...
        'Position',[80 100 130 40],'String','New Game','FontSize',12, ...
        'Callback',@new_game);
    uicontrol('Style','pushbutton','Parent',fig,'Units','pixels', ...
        'Position',[220 100 130 40],'String','Flip Board','FontSize',12, ...
        'Callback',@flip_board);
    uicontrol('Style','pushbutton','Parent',fig,'Units','pixels', ...
        'Position',[360 100 130 40],'String','History','FontSize',12, ...
        'FontWeight','bold','Callback',@open_history);
    uicontrol('Style','pushbutton','Parent',fig,'Units','pixels', ...
        'Position',[500 100 130 40],'String','Export PGN','FontSize',12, ...
        'FontWeight','bold','ForegroundColor',[0.9 0.8 0.3], ...
        'Callback',@export_pgn);

    clock_timer=[];
    if s.clock_enabled
        try
            clock_timer=timer('ExecutionMode','fixedSpacing','Period',0.15, ...
                'BusyMode','drop','TimerFcn',@tick_clock);
            start(clock_timer);
        catch, clock_timer=[]; end
    end
    update_clocks(); redraw();
    if s.human==-1, engine_play();
    else, set(status,'String',sprintf('Your move  [%s]',s.style_name)); end

    function elo_changed(src,~), s.elo=elos(get(src,'Value')); end
    function flip_board(~,~), s.flip=~s.flip; redraw(); end
    function on_close(~,~)
        try
            if ~isempty(clock_timer)&&isa(clock_timer,'timer')
                stop(clock_timer); delete(clock_timer);
            end
        catch, end
        delete(fig);
    end
    function new_game(~,~)
        s.board=init_board(); s.side=1; s.castling=[1 1 1 1]; s.en_passant=[0 0];
        s.selected=[0 0]; s.legal_sel=zeros(0,4); s.game_over=false;
        s.ply=0; s.history=[]; s.game_result='*';
        s.clock_w=cfg.clock_base; s.clock_b=cfg.clock_base; s.last_tick=tic;
        set(analysis_box,'String',''); set(subinfo_box,'String','');
        update_clocks(); redraw();
        if s.human==-1, engine_play();
        else, set(status,'String',sprintf('Your move  [%s]',s.style_name)); end
    end
    function account_elapsed()
        if ~s.clock_enabled, return; end
        now_t=toc(s.last_tick); s.last_tick=tic;
        if s.side==1, s.clock_w=max(0,s.clock_w-now_t);
        else, s.clock_b=max(0,s.clock_b-now_t); end
    end
    function switch_turn()
        account_elapsed();
        if s.clock_enabled
            if s.side==1, s.clock_w=s.clock_w+s.clock_inc;
            else, s.clock_b=s.clock_b+s.clock_inc; end
        end
        s.side=-s.side; s.last_tick=tic; update_clocks();
    end
    function tick_clock(~,~)
        if ~s.clock_enabled||s.game_over, return; end
        account_elapsed(); update_clocks();
        if s.clock_w<=0&&s.clock_b<=0
            s.game_over=true; s.game_result='1/2-1/2';
            set(status,'String','Both flags fell — Draw');
        elseif s.clock_w<=0
            s.game_over=true; s.game_result='0-1';
            set(status,'String','White flag fell — Black wins on time!');
        elseif s.clock_b<=0
            s.game_over=true; s.game_result='1-0';
            set(status,'String','Black flag fell — White wins on time!');
        end
    end
    function update_clocks()
        if ~s.clock_enabled
            set(black_clock_ui,'String','Black --:--');
            set(white_clock_ui,'String','White --:--'); return;
        end
        set(black_clock_ui,'String',sprintf('Black %s',fmt_time(s.clock_b)));
        set(white_clock_ui,'String',sprintf('White %s',fmt_time(s.clock_w)));
    end
    function ph = game_phase()
        heavy=0;
        for rr=1:8, for cc=1:8
            t=abs(s.board(rr,cc)); if t>=2&&t<=5, heavy=heavy+1; end
        end, end
        if s.ply<16, ph='Opening';
        elseif heavy>=6, ph='Middlegame';
        else, ph='Endgame'; end
    end
    function record_move_quiet(bb,m,side,cb,eb)
        san = move_to_san(bb,m,side,cb,eb);
        entry = struct('board',bb,'move',m,'side',side,'castling',cb,'ep',eb, ...
            'san',san,'label','','color',[1 1 1],'cp_loss',0, ...
            'best',m,'best_score',0,'played_score',0,'analyzed',false);
        if isempty(s.history), s.history=entry;
        else, s.history(end+1)=entry; end
    end
    function open_history(~,~)
        if isempty(s.history), msgbox('Chưa có nước nào.','History'); return; end
        show_analysis_window(s.history, 'Game History', s.use_mex);
    end
    function export_pgn(~,~)
        if isempty(s.history)
            msgbox('Chưa có nước nào để xuất.','Export PGN'); return;
        end
        white_name = ternary(s.human==1, 'Player', sprintf('Engine-%s-%d', s.engine_name, s.elo));
        black_name = ternary(s.human==-1, 'Player', sprintf('Engine-%s-%d', s.engine_name, s.elo));
        pgn_str = build_pgn_string(s.history, s.game_result, ...
            white_name, black_name, s.style_name, cfg.clock_label, s.engine_name);
        [fname, fpath] = uiputfile({'*.pgn','PGN files'}, ...
            'Save PGN', 'chess_game.pgn');
        if isequal(fname, 0), return; end
        fullname = fullfile(fpath, fname);
        fid = fopen(fullname, 'w');
        if fid < 0
            msgbox('Không thể mở file để ghi.','Error'); return;
        end
        fprintf(fid, '%s\n', pgn_str);
        fclose(fid);
        msgbox(sprintf('Đã lưu:\n%s', fullname), 'Export PGN');
    end
    function click_cb(~,~)
        if s.game_over, return; end
        if s.side~=s.human, return; end
        account_elapsed(); update_clocks();
        if s.clock_enabled&&(s.clock_w<=0||s.clock_b<=0), tick_clock([],[]); return; end
        try, cp=get(ax,'CurrentPoint'); catch, return; end
        x=cp(1,1); y=cp(1,2);
        if ~isfinite(x)||~isfinite(y), return; end
        dr=round(x); rr=9-round(y);
        if dr<1||dr>8||rr<1||rr>8, return; end
        if s.flip, c=9-dr; r=9-rr; else, c=dr; r=rr; end
        if s.selected(1)~=0
            for k=1:size(s.legal_sel,1)
                m=s.legal_sel(k,:);
                if m(3)==r && m(4)==c
                    bb=s.board; cb=s.castling; eb=s.en_passant;
                    apply_human_move(m);
                    s.selected=[0 0]; s.legal_sel=zeros(0,4); s.ply=s.ply+1;
                    redraw();
                    record_move_quiet(bb, m, s.human, cb, eb);
                    if is_game_over(), return; end
                    engine_play();
                    if is_game_over(), return; end
                    set(status,'String',sprintf('Your move  [%s]',s.style_name));
                    set(analysis_box,'String',''); set(subinfo_box,'String','');
                    return;
                end
            end
        end
        p=s.board(r,c);
        if p*s.side>0
            s.selected=[r c];
            allm=legal_moves(s.board,s.side,s.castling,s.en_passant);
            mask=allm(:,1)==r & allm(:,2)==c;
            s.legal_sel=allm(mask,:); redraw();
        else
            s.selected=[0 0]; s.legal_sel=zeros(0,4); redraw();
        end
    end
    function apply_human_move(m)
        p=s.board(m(1),m(2)); promo=5;
        if abs(p)==1&&(m(3)==8||m(3)==1), promo=ask_promotion(); end
        [s.board,s.castling,s.en_passant]=make_move(s.board,m,s.castling,s.en_passant,promo);
        switch_turn();
    end
    function engine_play()
        if s.game_over, return; end
        set(status,'String','Engine thinking...'); drawnow;
        bb=s.board; cb=s.castling; eb=s.en_passant; sb=s.side;
        [m,sc,d,src,promo]=engine_choose_move(s);
        if isempty(m)||~any(m), return; end
        [s.board,s.castling,s.en_passant]=make_move(s.board,m,s.castling,s.en_passant,promo);
        s.ply=s.ply+1; switch_turn(); redraw();
        record_move_quiet(bb, m, sb, cb, eb);
        if strcmp(src,'book')
            set(status,'String','Your move   (opening book)');
        elseif strcmp(src,'tb')
            set(status,'String','Your move   (tablebase)');
        else
            set(status,'String',sprintf('Your move   (%s, d%d, %+.2f)', ...
                s.style_name,d,sc/100));
        end
        set(subinfo_box,'String',sprintf('Engine: %s | %s', move_str(m), game_phase()));
    end
    function over = is_game_over()
        over=false;
        moves=legal_moves(s.board,s.side,s.castling,s.en_passant);
        if isempty(moves)
            over=true; s.game_over=true;
            if is_in_check(s.board,s.side)
                if s.side==1
                    s.game_result='0-1'; set(status,'String','Checkmate! Black wins.');
                else
                    s.game_result='1-0'; set(status,'String','Checkmate! White wins.');
                end
            else
                s.game_result='1/2-1/2'; set(status,'String','Stalemate - Draw');
            end
        elseif is_in_check(s.board,s.side)&&s.side==s.human
            set(status,'String','CHECK! Your move');
        end
    end
    function cp_white = quick_eval()
        cp_white = evaluate(s.board);
        if s.use_mex
            try
                [~,sc,~]=engine_search_mex(s.board,s.side,double(s.castling), ...
                    double(s.en_passant),double(3),double(0.25),[100 100 100 100 100]);
                cp_white = sc*s.side;
            catch
                try
                    [~,sc,~]=engine_search(s.board,s.side,double(s.castling), ...
                        double(s.en_passant),double(3),double(0.25),[100 100 100 100 100]);
                    cp_white = sc*s.side;
                catch, end
            end
        end
    end
    function update_eval_bar()
        cp=quick_eval(); MATE=100000;
        if cp>MATE-1000, pct=0.98; label='MATE+';
        elseif cp<-(MATE-1000), pct=0.02; label='MATE-';
        else
            pct=0.5+0.5*tanh(cp/400); pct=max(0.04,min(0.96,pct));
            label=sprintf('%+.2f',cp/100);
        end
        cla(eval_ax); hold(eval_ax,'on');
        patch('Parent',eval_ax,'XData',[0 pct pct 0],'YData',[0 0 1 1], ...
            'FaceColor',[0.95 0.95 0.95],'EdgeColor','none','HitTest','off');
        patch('Parent',eval_ax,'XData',[pct 1 1 pct],'YData',[0 0 1 1], ...
            'FaceColor',[0.10 0.10 0.10],'EdgeColor','none','HitTest','off');
        text(0.5,0.5,label,'Parent',eval_ax,'HorizontalAlignment','center', ...
            'VerticalAlignment','middle','FontSize',12,'FontWeight','bold', ...
            'Color',[0.95 0.95 0.4],'HitTest','off');
        drawnow;
    end
    function redraw()
        cla(ax);
        img=render_board(s);
        hIm=image('Parent',ax,'XData',[0.5 8.5],'YData',[0.5 8.5],'CData',img);
        try, set(hIm,'HitTest','off'); catch, end
        try, set(hIm,'PickableParts','none'); catch, end
        set(ax,'YDir','reverse','XLim',[0.5 8.5],'YLim',[0.5 8.5], ...
            'XTick',[],'YTick',[],'Box','on');
        hold(ax,'on');
        draw_rect=@(xr,yr,col,alpha) patch('Parent',ax, ...
            'XData',[xr(1) xr(2) xr(2) xr(1)],'YData',[yr(1) yr(1) yr(2) yr(2)], ...
            'FaceColor',col,'FaceAlpha',alpha,'EdgeColor','none','HitTest','off');
        if s.selected(1)~=0
            [y,dc]=disp_pos(s.selected(1),s.selected(2),s.flip);
            draw_rect([dc-0.5 dc+0.5],[y-0.5 y+0.5],[1 1 0.2],0.40);
        end
        for k=1:size(s.legal_sel,1)
            m=s.legal_sel(k,:);
            [y,dc]=disp_pos(m(3),m(4),s.flip);
            draw_rect([dc-0.16 dc+0.16],[y-0.16 y+0.16],[0 0.75 0],0.55);
        end
        drawnow; update_eval_bar();
    end
end

function [m,sc,d,src,promo] = engine_choose_move(s)
    m=[]; sc=0; d=0; src='none'; promo=5;

    mv=book_lookup(s.book,s.board,s.side,s.castling,s.en_passant);
    if ~isempty(mv)&&s.ply<24, m=mv; src='book'; return; end

    [found,tb_score,tb_move]=endgame_probe(s.board,s.side,s.castling,s.en_passant);
    if found&&any(tb_move), m=tb_move; sc=tb_score*s.side; src='tb'; return; end

    [depth,noise,tlim]=elo_to_params(s.elo);
    if s.clock_enabled
        remaining=(s.side==1)*s.clock_w+(s.side==-1)*s.clock_b;
        tlim=min(tlim,max(0.3,remaining/20));
    end
    if rand()<noise
        moves=legal_moves(s.board,s.side,s.castling,s.en_passant);
        m=moves(randi(size(moves,1)),:); src='random'; return;
    end
    if s.use_mex
        try
            [mv,scv,dv]=engine_search_mex(s.board,s.side,double(s.castling), ...
                double(s.en_passant),double(depth),double(tlim),double(s.style));
            m=round(mv(:)'); sc=scv; d=dv; src='search';
            return;
        catch err, fprintf('MEX error: %s\n',err.message); end
    end

    [m,sc,info]=find_best_matlab(s.board,s.side,s.castling,s.en_passant,depth,tlim);
    d=info.depth; src='search';
end

function [depth,noise,tlim] = elo_to_params(elo)
    M=[800 2 0.55 0.6; 1000 3 0.35 0.8; 1200 4 0.20 1.0; 1400 5 0.10 1.5;
       1600 6 0.05 2.0; 1800 7 0.02 3.0; 2000 8 0.00 4.0; 2200 10 0.00 6.0;
       2400 12 0.00 9.0; 2600 16 0.00 12.0; 2800 20 0.00 18.0;
       3000 24 0.00 25.0; 3200 30 0.00 40.0];
    depth=6; noise=0; tlim=3;
    for i=1:size(M,1)
        if M(i,1)==elo, depth=M(i,2); noise=M(i,3); tlim=M(i,4); return; end
    end
end

function book = build_book()
    L={};
    L{end+1}=[2 5 4 5;7 5 5 5;1 7 3 6;8 2 6 3;1 6 3 4;8 6 6 4;2 3 3 3;8 7 6 6;2 4 4 4;6 4 4 4];
    L{end+1}=[2 5 4 5;7 5 5 5;1 7 3 6;8 2 6 3;1 6 3 5;7 1 6 1;3 5 4 4;8 7 6 6;1 5 1 7];
    L{end+1}=[2 5 4 5;7 5 5 5;1 7 3 6;8 2 6 3;2 4 4 4;5 5 4 4;3 6 4 4;8 7 6 6;1 2 3 3;6 4 5 4];
    L{end+1}=[2 5 4 5;7 5 5 5;1 7 3 6;8 2 6 3;3 4 4 4;8 7 6 6];
    L{end+1}=[2 5 4 5;7 3 5 3;1 7 3 6;7 4 6 4;2 4 4 4;5 3 4 4;3 6 4 4;8 7 6 6;1 2 3 3];
    L{end+1}=[2 5 4 5;7 3 5 3;1 7 3 6;8 2 6 3;2 4 4 4;5 3 4 4;3 6 4 4;8 7 6 6;1 2 3 3;7 7 6 7];
    L{end+1}=[2 5 4 5;7 3 5 3;1 7 3 6;7 5 6 5;2 4 4 4;7 4 5 4];
    L{end+1}=[2 5 4 5;7 5 6 5;2 4 4 4;7 4 5 4;1 2 3 3;8 7 6 6];
    L{end+1}=[2 5 4 5;7 5 6 5;2 4 4 4;7 4 5 4;2 5 3 5;7 3 6 3];
    L{end+1}=[2 5 4 5;7 3 6 3;2 4 4 4;7 4 5 4;1 2 3 3;7 3 5 3;8 7 6 6];
    L{end+1}=[2 5 4 5;7 4 5 4;2 5 3 5;8 4 6 4;2 4 4 4;8 7 6 6];
    L{end+1}=[2 4 4 4;7 4 5 4;2 3 4 3;7 5 6 5;1 2 3 3;8 7 6 6;1 6 3 5];
    L{end+1}=[2 4 4 4;7 4 5 4;2 3 4 3;7 3 6 3;1 7 3 6;8 7 6 6;1 2 3 3];
    L{end+1}=[2 4 4 4;8 7 6 6;2 3 4 3;7 7 6 7;1 2 3 3;8 6 6 7;2 5 4 5;7 4 6 4];
    L{end+1}=[2 4 4 4;8 7 6 6;2 3 4 3;7 5 6 5;1 2 3 3;8 6 6 2;2 5 4 5;7 4 6 4];
    L{end+1}=[2 3 4 3;7 5 5 5;1 2 3 3;8 7 6 6;1 7 3 6];
    L{end+1}=[1 7 3 6;7 4 5 4;2 3 4 3;7 5 6 5;2 4 4 4];
    L{end+1}=[2 5 4 5;7 4 6 4;2 4 4 4;8 7 6 6;1 2 3 3;7 7 6 7];
    L{end+1}=[2 5 4 5;7 7 6 7;2 4 4 4;8 6 6 7;1 2 3 3;7 4 6 4];
    book=containers.Map('KeyType','char','ValueType','any');
    for i=1:numel(L)
        seq=L{i}; b=init_board(); cast=[1 1 1 1]; ep=[0 0]; side=1;
        for k=1:size(seq,1)
            mv=seq(k,:); key=hash_key(b,side,cast,ep);
            if ~isKey(book,key), book(key)=mv; end
            [b,cast,ep]=make_move(b,mv,cast,ep,5); side=-side;
        end
    end
end

function key = hash_key(b,side,cast,ep)
    key=['B' num2str(b(:)') '|S' num2str(side) '|C' num2str(cast(:)') '|E' num2str(ep(:)')];
end

function mv = book_lookup(book,b,side,cast,ep)
    mv=[]; key=hash_key(b,side,cast,ep);
    if isKey(book,key), mv=book(key); end
end

function [found,score,best_move] = endgame_probe(board,side,castling,ep)
    found=false; score=0; best_move=zeros(1,4);
    wq=0;wr=0;wb=0;wn=0;wp=0;wk=[0 0]; bq=0;br=0;bb=0;bn=0;bp=0;bk=[0 0];
    for r=1:8, for c=1:8
        v=board(r,c);
        switch v
            case 1, wp=wp+1; case 2, wn=wn+1; case 3, wb=wb+1; case 4, wr=wr+1;
            case 5, wq=wq+1; case 6, wk=[r c];
            case -1, bp=bp+1; case -2, bn=bn+1; case -3, bb=bb+1; case -4, br=br+1;
            case -5, bq=bq+1; case -6, bk=[r c];
        end
    end, end
    wm=wp+wn+wb+wr+wq; bm=bp+bn+bb+br+bq;
    if wm==0&&bm==0, found=true; score=0; best_move=pick_pass(board,side,castling,ep); return; end
    if wm==1&&bm==0&&wp==0&&wq==0&&wr==0, found=true; score=0; best_move=pick_pass(board,side,castling,ep); return; end
    if bm==1&&wm==0&&bp==0&&bq==0&&br==0, found=true; score=0; best_move=pick_pass(board,side,castling,ep); return; end
    if wq==1&&wm==1&&bm==0
        e=min([bk(1),9-bk(1),bk(2),9-bk(2)]); found=true; score=9000+(4-e)*50;
        best_move=pick_pass(board,side,castling,ep); return;
    end
    if bq==1&&bm==1&&wm==0
        e=min([wk(1),9-wk(1),wk(2),9-wk(2)]); found=true; score=-(9000+(4-e)*50);
        best_move=pick_pass(board,side,castling,ep); return;
    end
    if wr==1&&wm==1&&bm==0
        e=min([bk(1),9-bk(1),bk(2),9-bk(2)]); found=true; score=6000+(4-e)*50;
        best_move=pick_pass(board,side,castling,ep); return;
    end
    if br==1&&bm==1&&wm==0
        e=min([wk(1),9-wk(1),wk(2),9-wk(2)]); found=true; score=-(6000+(4-e)*50);
        best_move=pick_pass(board,side,castling,ep); return;
    end
end

function mv = pick_pass(board,side,castling,ep)
    moves=legal_moves(board,side,castling,ep);
    if isempty(moves), mv=zeros(1,4); return; end
    best=-inf; mv=moves(1,:);
    for i=1:size(moves,1)
        [nb,~,~]=make_move(board,moves(i,:),castling,ep,5);
        nk=find(nb==-side*6,1);
        if isempty(nk), s=1e9; else, [r,c]=ind2sub([8 8],nk); s=abs(r-4.5)+abs(c-4.5); end
        if s>best, best=s; mv=moves(i,:); end
    end
end

function [label,col,cp_loss,best_move,best_score,played_score] = ...
        analyze_move(bb,played,side,castling,ep,use_mex,depth,tlim)
    if nargin < 7, depth = 8; end
    if nargin < 8, tlim  = 2.0; end
    MATE = 100000; MATE_TH = 90000;
    best_move = zeros(1,4); best_score = 0; played_score = 0;
    label = ''; col = [0.7 0.7 0.7]; cp_loss = 0;
    moves = legal_moves(bb, side, castling, ep);
    if isempty(moves), label='No moves'; return; end
    if ~any(all(moves==played,2)), label='?'; return; end

    [nb_p, nc_p, ne_p] = make_move(bb, played, castling, ep, 5);
    opp_moves = legal_moves(nb_p, -side, nc_p, ne_p);
    played_is_mate = isempty(opp_moves) && is_in_check(nb_p, -side);
    played_allows_mate1 = false;
    if ~played_is_mate
        for k = 1:size(opp_moves,1)
            om = opp_moves(k,:);
            [nb2, nc2, ne2] = make_move(nb_p, om, nc_p, ne_p, 5);
            m2 = legal_moves(nb2, side, nc2, ne2);
            if isempty(m2) && is_in_check(nb2, side)
                played_allows_mate1 = true; break;
            end
        end
    end
    best_is_mate1 = false; best_mate_move = zeros(1,4);
    for k = 1:size(moves,1)
        m = moves(k,:);
        [nb3, nc3, ne3] = make_move(bb, m, castling, ep, 5);
        m3 = legal_moves(nb3, -side, nc3, ne3);
        if isempty(m3) && is_in_check(nb3, -side)
            best_is_mate1 = true; best_mate_move = m; break;
        end
    end

    if played_is_mate
        label='MATE FOUND!!'; col=[0.2 1 0.4];
        best_score=MATE; played_score=MATE; cp_loss=0; best_move=played; return;
    end
    if played_allows_mate1
        label='ALLOWS MATE-IN-1'; col=[1 0.2 0.2];
        best_move=best_mate_move; best_score=MATE; played_score=-MATE; cp_loss=MATE;
        return;
    end
    if best_is_mate1
        label='MISSED MATE!'; col=[1 0.35 0.35];
        best_move=best_mate_move; best_score=MATE; played_score=0; cp_loss=MATE;
        return;
    end

    if use_mex
        try
            style = [100 100 100 100 100];
            [nb, nc, ne] = make_move(bb, played, castling, ep, 5);
            try
                [bmv, bsc, ~] = engine_search_mex(bb, side, ...
                    double(castling), double(ep), double(depth), double(tlim), style);
                [~, opp_score, ~] = engine_search_mex(nb, -side, ...
                    double(nc), double(ne), max(depth-2, 4), double(tlim*0.7), style);
            catch
                [bmv, bsc, ~] = engine_search(bb, side, ...
                    double(castling), double(ep), double(depth), double(tlim), style);
                [~, opp_score, ~] = engine_search(nb, -side, ...
                    double(nc), double(ne), max(depth-2, 4), double(tlim*0.7), style);
            end
            bm_eng = round(bmv(:)');
            if any(bm_eng), best_move = bm_eng; end
            best_score = bsc;
            played_score = -opp_score;
            if ~isfinite(best_score),   best_score = 0; end
            if ~isfinite(played_score), played_score = best_score; end
            cp_loss = max(0, best_score - played_score);
            if ~isfinite(cp_loss), cp_loss = 0; end
            [label, col] = classify_final(cp_loss, best_score, played_score, ...
                bb, played, side, castling, ep);
            return;
        catch err
            fprintf('MEX analysis error: %s\n', err.message);
            use_mex = false;
        end
    end

    [best_score, best_move, played_score] = ...
        analyze_pure(bb, played, side, castling, ep, 3, 3.0);
    if ~isfinite(best_score),   best_score = 0; end
    if ~isfinite(played_score), played_score = best_score; end
    cp_loss = max(0, best_score - played_score);
    if ~isfinite(cp_loss), cp_loss = 0; end
    [label, col] = classify_final(cp_loss, best_score, played_score, ...
        bb, played, side, castling, ep);
end

function [label, col] = classify_final(cp_loss, best_score, played_score, ...
        bb, played, side, castling, ep)
    n_legal = size(legal_moves(bb, side, castling, ep), 1);
    if n_legal == 1, label = 'Forced'; col = [0.7 0.7 0.7]; return; end
    mat_bef = material_bal(bb, side);
    [b_after,~,~] = make_move(bb, played, castling, ep, 5);
    mat_delta = material_bal(b_after, side) - mat_bef;
    if cp_loss <= 10
        if mat_delta <= -200 && played_score > 100
            label='Brilliant!!'; col=[0.2 1 0.4];
        elseif mat_delta <= -100 && played_score > 150
            label='Great move!'; col=[0.4 0.75 1];
        else
            label='Best move'; col=[0.3 0.95 0.3];
        end
    elseif cp_loss <= 50, label='Excellent'; col=[0.55 0.9 0.55];
    elseif cp_loss <= 100, label='Good'; col=[0.85 0.9 0.5];
    elseif cp_loss <= 200, label='Inaccuracy'; col=[1 0.9 0.3];
    elseif cp_loss <= 400, label='Mistake'; col=[1 0.6 0.2];
    else, label='Blunder'; col=[1 0.3 0.3]; end
end

function [label,col,cp_loss,best_move,best_score,played_score] = ...
        deep_analyze(bb,played,side,castling,ep,use_mex)
    if use_mex
        try
            [label,col,cp_loss,best_move,best_score,played_score] = ...
                analyze_move(bb, played, side, castling, ep, true, 14, 12.0);
            if isfinite(cp_loss), return; end
        catch, end
    end
    try
        [label,col,cp_loss,best_move,best_score,played_score] = ...
            analyze_move(bb, played, side, castling, ep, use_mex, 8, 6.0);
    catch
        label='?'; col=[1 0.3 0.3]; cp_loss=0;
        best_move=played; best_score=0; played_score=0;
    end
end

function [best_score,best_move,played_score] = analyze_pure(bb,played,side,castling,ep,depth,tlim)
    global ANA_TSTART ANA_TLIMIT ANA_ABORT
    ANA_TSTART=tic; ANA_TLIMIT=tlim; ANA_ABORT=false;
    moves=legal_moves(bb,side,castling,ep);
    scores=-inf*ones(size(moves,1),1);
    pi = find(all(moves==played,2),1);
    if ~isempty(pi)
        m = moves(pi,:);
        [nb,nc,ne]=make_move(bb,m,castling,ep,5);
        scores(pi)=-negamax_anal(nb,-side,nc,ne,depth-1,-inf,inf,1);
    end
    for i=1:size(moves,1)
        if i == pi, continue; end
        if ANA_ABORT || toc(ANA_TSTART)>ANA_TLIMIT, break; end
        m=moves(i,:);
        [nb,nc,ne]=make_move(bb,m,castling,ep,5);
        scores(i)=-negamax_anal(nb,-side,nc,ne,depth-1,-inf,inf,1);
    end
    valid = scores > -inf;
    if ~any(valid)
        best_score = 0; best_move = played; played_score = 0; return;
    end
    scores_v = scores; scores_v(~valid) = -inf;
    [best_score, bi] = max(scores_v);
    best_move = moves(bi,:);
    if isempty(pi) || ~valid(pi), played_score = best_score;
    else, played_score = scores(pi); end
end

function m = material_bal(board,side)
    vals=[100 320 330 500 900 0]; m=0;
    for r=1:8, for c=1:8
        p=board(r,c); if p==0, continue; end
        if sign(p)==side, m=m+vals(abs(p)); else, m=m-vals(abs(p)); end
    end, end
end

function sc = negamax_anal(board,side,castling,ep,depth,alpha,beta,ply)
    global ANA_ABORT ANA_TSTART ANA_TLIMIT
    MATE=100000;
    if ANA_ABORT||toc(ANA_TSTART)>ANA_TLIMIT, ANA_ABORT=true; sc=0; return; end
    moves=legal_moves(board,side,castling,ep);
    if isempty(moves)
        if is_in_check(board,side), sc=-MATE-depth; else, sc=0; end
        return;
    end
    if depth<=0, sc=quiesce_anal(board,side,castling,ep,alpha,beta,ply); return; end
    best=-inf;
    for i=1:size(moves,1)
        if ANA_ABORT, best=0; break; end
        [nb,nc,ne]=make_move(board,moves(i,:),castling,ep,5);
        s=-negamax_anal(nb,-side,nc,ne,depth-1,-beta,-alpha,ply+1);
        if s>best, best=s; end
        if s>alpha, alpha=s; end
        if alpha>=beta, break; end
    end
    sc=best;
end

function sc = quiesce_anal(board,side,castling,ep,alpha,beta,~)
    global ANA_ABORT ANA_TSTART ANA_TLIMIT
    if ANA_ABORT||toc(ANA_TSTART)>ANA_TLIMIT, ANA_ABORT=true; sc=0; return; end
    stand=evaluate(board)*side;
    if stand>=beta, sc=beta; return; end
    if stand>alpha, alpha=stand; end
    moves=legal_moves(board,side,castling,ep);
    caps=zeros(size(moves,1),4); n=0;
    for i=1:size(moves,1)
        m=moves(i,:);
        if board(m(3),m(4))~=0||(abs(board(m(1),m(2)))==1&&m(2)~=m(4))
            n=n+1; caps(n,:)=m;
        end
    end
    caps=caps(1:n,:); best=stand;
    for i=1:size(caps,1)
        if ANA_ABORT, sc=best; return; end
        [nb,nc,ne]=make_move(board,caps(i,:),castling,ep,5);
        s=-quiesce_anal(nb,-side,nc,ne,-beta,-alpha,0);
        if s>best, best=s; end
        if s>alpha, alpha=s; end
        if alpha>=beta, break; end
    end
    sc=best;
end

function cfg = start_menu()
    cfg=[];
    elos=[800 1000 1200 1400 1600 1800 2000 2200 2400 2600 2800 3000 3200];
    styles={'Balanced',[100 100 100 100 100]; 'Mikhail Tal',[180 220 250 60 180];
        'Paul Morphy',[170 180 180 80 160]; 'Garry Kasparov',[160 170 150 90 150];
        'Bobby Fischer',[140 140 120 130 120]; 'Magnus Carlsen',[110 120 100 140 100];
        'Emanuel Lasker',[120 110 130 120 130]; 'Anatoly Karpov',[95 100 80 150 80];
        'Jose R. Capablanca',[90 90 60 160 70]; 'Tigran Petrosian',[60 80 40 140 50]};
    clocks={'No clock',0,0; 'Bullet 1+0',60,0; 'Bullet 2+1',120,1;
        'Blitz 3+2',180,2; 'Blitz 5+0',300,0; 'Blitz 5+3',300,3;
        'Rapid 10+0',600,0; 'Rapid 10+5',600,5; 'Classical 30+0',1800,0};

    f=figure('Name','Chess vs Engine — Menu','NumberTitle','off', ...
        'MenuBar','none','ToolBar','none','Resize','off','Position',[480 130 540 640], ...
        'Color',[0.10 0.10 0.12],'WindowStyle','modal');

    uicontrol('Style','text','Parent',f,'Units','pixels','Position',[80 580 380 40], ...
        'String','CHESS VS ENGINE','FontSize',22,'FontWeight','bold', ...
        'BackgroundColor',[0.10 0.10 0.12],'ForegroundColor',[0.9 0.9 1]);

    uicontrol('Style','text','Parent',f,'Units','pixels','Position',[60 530 200 25], ...
        'String','Engine ELO:','FontSize',13,'BackgroundColor',[0.10 0.10 0.12], ...
        'ForegroundColor',[1 1 1],'HorizontalAlignment','left');
    hElo=uicontrol('Style','popupmenu','Parent',f,'Units','pixels', ...
        'Position',[260 530 220 28],'String',cellstr(num2str(elos(:))), ...
        'Value',6,'FontSize',12);

    uicontrol('Style','text','Parent',f,'Units','pixels','Position',[60 485 200 25], ...
        'String','Chess engine:','FontSize',13,'BackgroundColor',[0.10 0.10 0.12], ...
        'ForegroundColor',[1 1 1],'HorizontalAlignment','left');
    uicontrol('Style','text','Parent',f,'Units','pixels','Position',[260 485 220 28], ...
        'String','MEX Engine v11','FontSize',12,'FontWeight','bold', ...
        'BackgroundColor',[0.18 0.18 0.22],'ForegroundColor',[0.3 0.95 0.3], ...
        'HorizontalAlignment','center');

    uicontrol('Style','text','Parent',f,'Units','pixels','Position',[60 440 200 25], ...
        'String','Player style:','FontSize',13,'BackgroundColor',[0.10 0.10 0.12], ...
        'ForegroundColor',[1 1 1],'HorizontalAlignment','left');
    hStyle=uicontrol('Style','popupmenu','Parent',f,'Units','pixels', ...
        'Position',[260 440 220 28],'String',styles(:,1),'Value',1,'FontSize',12);

    uicontrol('Style','text','Parent',f,'Units','pixels','Position',[60 395 200 25], ...
        'String','You play as:','FontSize',13,'BackgroundColor',[0.10 0.10 0.12], ...
        'ForegroundColor',[1 1 1],'HorizontalAlignment','left');
    hSide=uicontrol('Style','popupmenu','Parent',f,'Units','pixels', ...
        'Position',[260 395 220 28],'String',{'White','Black'},'Value',1,'FontSize',12);

    uicontrol('Style','text','Parent',f,'Units','pixels','Position',[60 350 200 25], ...
        'String','Time control:','FontSize',13,'BackgroundColor',[0.10 0.10 0.12], ...
        'ForegroundColor',[1 1 1],'HorizontalAlignment','left');
    cnames=cell(size(clocks,1),1);
    for i=1:size(clocks,1), cnames{i}=clocks{i,1}; end
    hClock=uicontrol('Style','popupmenu','Parent',f,'Units','pixels', ...
        'Position',[260 350 220 28],'String',cnames,'Value',7,'FontSize',12);

    uicontrol('Style','text','Parent',f,'Units','pixels','Position',[40 270 460 60], ...
        'String',sprintf(['MEX Engine v11: Tich hop Bitboard Magic & Alpha-Beta Search\n' ...
            'Analysis an khi choi, hien khi History hoac PGN.']), ...
        'FontSize',10,'BackgroundColor',[0.10 0.10 0.12],'ForegroundColor',[0.7 0.7 0.85], ...
        'HorizontalAlignment','center');

    uicontrol('Style','pushbutton','Parent',f,'Units','pixels','Position',[60 190 180 50], ...
        'String','Start Game','FontSize',14,'FontWeight','bold', ...
        'ForegroundColor',[0.3 0.95 0.3],'Callback',@on_start);
    uicontrol('Style','pushbutton','Parent',f,'Units','pixels','Position',[300 190 180 50], ...
        'String','Analyze PGN','FontSize',14,'FontWeight','bold', ...
        'ForegroundColor',[0.3 0.8 1],'Callback',@on_pgn);
    uicontrol('Style','pushbutton','Parent',f,'Units','pixels','Position',[130 110 130 45], ...
        'String','Quit','FontSize',13,'Callback',@on_quit);
    uicontrol('Style','pushbutton','Parent',f,'Units','pixels','Position',[280 110 130 45], ...
        'String','Help','FontSize',13,'Callback',@on_help);

    uiwait(f);
    if ishandle(f), delete(f); end

    function on_start(~,~)
        cfg=struct(); cfg.mode='game';
        cfg.elo=elos(get(hElo,'Value'));
        cfg.engine_choice=1;
        si=get(hStyle,'Value');
        cfg.style_name=styles{si,1}; cfg.style_vec=styles{si,2};
        if get(hSide,'Value')==1, cfg.human_side=1; else, cfg.human_side=-1; end
        ci=get(hClock,'Value');
        cfg.clock_base=clocks{ci,2}; cfg.clock_inc=clocks{ci,3};
        cfg.clock_label=clocks{ci,1};
        uiresume(f);
    end
    function on_pgn(~,~)
        cfg=struct(); cfg.mode='pgn';
        cfg.elo=1800; cfg.engine_choice=1;
        cfg.style_name='Balanced'; cfg.style_vec=[100 100 100 100 100];
        cfg.human_side=1; cfg.clock_base=0; cfg.clock_inc=0;
        cfg.clock_label='None';
        uiresume(f);
    end
    function on_quit(~,~), cfg=[]; uiresume(f); end
    function on_help(~,~)
        msgbox({ ...
            'Chess vs Engine v11 — Huong dan:'; '';
            '• Start Game: chon ELO, style, thoi gian'; ...
            '  - Engine: MEX Engine v11'; ...
            '• Analyze PGN: dan PGN tran ngoai de phan tich'; ...
            '• Export PGN: xuat tran vua dau'; '';
            'Trong luc dau: KHONG hien phan tich (tranh loi the).'; ...
            'Mo History sau khi dau de xem phan tich.'}, ...
            'Help');
    end
end

function pgn_str = pgn_dialog_get_string()
    pgn_str = '';
    dlg = figure('Name','Load PGN','NumberTitle','off','MenuBar','none', ...
        'ToolBar','none','Position',[300 200 700 550], ...
        'Color',[0.12 0.12 0.14],'WindowStyle','modal');
    uicontrol('Style','text','Parent',dlg,'Units','normalized', ...
        'Position',[0.03 0.92 0.94 0.06], ...
        'String','Dán PGN / chuỗi nước đi (SAN) của trận đấu ngoài:', ...
        'FontSize',12,'FontWeight','bold', ...
        'BackgroundColor',[0.12 0.12 0.14],'ForegroundColor',[1 1 1], ...
        'HorizontalAlignment','left');
    hEdit = uicontrol('Style','edit','Parent',dlg,'Units','normalized', ...
        'Position',[0.03 0.18 0.94 0.72],'Max',20,'Min',0, ...
        'String',sprintf(['Ví dụ:\n1. e4 e5 2. Nf3 Nc6 3. Bc4 Bc5 4. b4 Bxb4\n' ...
            '5. c3 Ba5 6. d4 exd4 7. O-O d3 8. Qb3 Qf6 9. e5 Qg6\n' ...
            '10. Re1 Nge7 11. Ba3 b5 12. Qxb5 Rb8 13. Qa4 Bb6']), ...
        'FontSize',11,'FontName','Courier New', ...
        'BackgroundColor',[0.18 0.18 0.20],'ForegroundColor',[1 1 1], ...
        'HorizontalAlignment','left');
    uicontrol('Style','pushbutton','Parent',dlg,'Units','normalized', ...
        'Position',[0.20 0.06 0.25 0.08],'String','Analyze','FontSize',13, ...
        'FontWeight','bold','ForegroundColor',[0.2 0.9 0.3],'Callback',@on_go);
    uicontrol('Style','pushbutton','Parent',dlg,'Units','normalized', ...
        'Position',[0.55 0.06 0.25 0.08],'String','Cancel','FontSize',13, ...
        'Callback',@on_cancel);
    uiwait(dlg);
    if ishandle(dlg), delete(dlg); end
    function on_go(~,~)
        txt = get(hEdit,'String');
        if ischar(txt), pgn_str = txt;
        else, pgn_str = strjoin(txt, sprintf('\n')); end
        pgn_str = strtrim(pgn_str);
        uiresume(dlg);
    end
    function on_cancel(~,~)
        pgn_str = ''; uiresume(dlg);
    end
end

function pgn_str = build_pgn_string(history, result, white_name, ...
                                     black_name, style_name, time_control, engine_name)
    if nargin < 7, engine_name = 'MATLAB MEX'; end
    lines = {};
    lines{end+1} = '[Event "Chess vs Engine"]';
    lines{end+1} = '[Site "MATLAB/Octave"]';
    lines{end+1} = sprintf('[Date "%s"]', datestr(now, 'yyyy.mm.dd'));
    lines{end+1} = '[Round "-"]';
    lines{end+1} = sprintf('[White "%s"]', white_name);
    lines{end+1} = sprintf('[Black "%s"]', black_name);
    lines{end+1} = sprintf('[Result "%s"]', result);
    lines{end+1} = sprintf('[EngineName "%s"]', engine_name);
    lines{end+1} = sprintf('[EngineStyle "%s"]', style_name);
    lines{end+1} = sprintf('[TimeControl "%s"]', time_control);
    lines{end+1} = '';
    txt = ''; mv_n = 1;
    for i = 1:numel(history)
        h = history(i);
        if h.side == 1
            piece = sprintf('%d. %s ', mv_n, h.san);
        else
            piece = sprintf('%s ', h.san);
            mv_n = mv_n + 1;
        end
        if length(txt) + length(piece) > 80
            txt = [txt sprintf('\n') piece];
        else
            txt = [txt piece];
        end
    end
    txt = strtrim(txt);
    lines{end+1} = txt;
    lines{end+1} = '';
    lines{end+1} = result;
    pgn_str = strjoin(lines, sprintf('\n'));
end

function run_pgn_analysis(pgn_str, use_mex)
    tokens = parse_pgn(pgn_str);
    if isempty(tokens)
        msgbox('PGN không hợp lệ.','Error'); return;
    end
    fprintf('Đã đọc %d nước.\n', numel(tokens));
    wb = waitbar(0, 'Đang dựng lại trận đấu...','Name','PGN Analysis');
    board = init_board(); side = 1; castling = [1 1 1 1]; ep = [0 0];
    game = struct('board',{},'move',{},'side',{},'castling',{},'ep',{}, ...
        'san',{},'label',{},'color',{},'cp_loss',{},'best',{}, ...
        'best_score',{},'played_score',{},'analyzed',{});
    n_bad = 0;
    for i = 1:numel(tokens)
        [mv, promo] = san_to_move(board, side, castling, ep, tokens{i});
        if isempty(mv)
            fprintf('Không đọc được nước %d: %s\n', i, tokens{i});
            n_bad = n_bad + 1;
            if n_bad > 3, break; end
            continue;
        end
        san_can = move_to_san(board, mv, side, castling, ep);
        e = struct('board',board,'move',mv,'side',side,'castling',castling, ...
            'ep',ep,'san',san_can,'label','','color',[1 1 1], ...
            'cp_loss',0,'best',mv,'best_score',0,'played_score',0,'analyzed',false);
        game(end+1) = e;
        [board, castling, ep] = make_move(board, mv, castling, ep, promo);
        side = -side;
    end
    if isempty(game), close(wb); msgbox('Không có nước hợp lệ.','Error'); return; end
    engine_label = 'MEX Engine v11';
    n = numel(game);
    for i = 1:n
        waitbar(i/(n+1), wb, sprintf('[%s] %d/%d (%s)...', ...
            engine_label, i, n, game(i).san));
        try
            [lbl, col, loss, bm, bsc, psc] = analyze_move( ...
                game(i).board, game(i).move, game(i).side, ...
                game(i).castling, game(i).ep, use_mex, 14, 3.0);
            game(i).label = lbl; game(i).color = col;
            game(i).cp_loss = loss; game(i).best = bm;
            game(i).best_score = bsc; game(i).played_score = psc;
            game(i).analyzed = true;
        catch err
            fprintf('Lỗi phân tích nước %d: %s\n', i, err.message);
        end
    end
    waitbar(1, wb, 'Done'); drawnow; close(wb);
    show_analysis_window(game, 'PGN Analysis', use_mex);
end

function tokens = parse_pgn(pgn_str)
    if iscell(pgn_str), pgn_str = strjoin(pgn_str, sprintf('\n')); end
    s = pgn_str;
    s = regexprep(s, '\[[^\]]*\]', ' ');
    s = regexprep(s, '\{[^}]*\}', ' ');
    s = regexprep(s, '\([^)]*\)', ' ');
    s = regexprep(s, '\$\d+', ' ');
    s = regexprep(s, '\s(1-0|0-1|1/2-1/2|\*)\s', ' ');
    s = regexprep(s, '[\r\n\t]+', ' ');
    raw = strsplit(strtrim(s));
    tokens = {};
    for i = 1:numel(raw)
        t = raw{i};
        t = regexprep(t, '^\d+\.+', '');
        t = regexprep(t, '\.+$', '');
        t = strtrim(t);
        if ~isempty(t), tokens{end+1} = t; end
    end
end

function [mv, promo] = san_to_move(board, side, castling, ep, san)
    mv = []; promo = 5;
    if isempty(san), return; end
    s = san;
    s = regexprep(s, '[+#!?]+$', '');
    s = regexprep(s, '^[+#!?]+', '');
    if isempty(s), return; end
    files = 'abcdefgh';
    if any(strcmp(s, {'O-O','0-0'}))
        if side == 1, mv = [1 5 1 7]; else, mv = [8 5 8 7]; end
        return;
    end
    if any(strcmp(s, {'O-O-O','0-0-0'}))
        if side == 1, mv = [1 5 1 3]; else, mv = [8 5 8 3]; end
        return;
    end
    if length(s) >= 2 && s(end-1) == '='
        switch upper(s(end))
            case 'Q', promo = 5; case 'R', promo = 4;
            case 'B', promo = 3; case 'N', promo = 2;
        end
        s = s(1:end-2);
    end
    piece_type = 1;
    if ~isempty(s) && any(s(1) == 'NBRQK')
        switch s(1)
            case 'N', piece_type = 2; case 'B', piece_type = 3;
            case 'R', piece_type = 4; case 'Q', piece_type = 5;
            case 'K', piece_type = 6;
        end
        s = s(2:end);
    end
    if length(s) < 2, return; end
    to_file_idx = find(files == lower(s(end-1)), 1);
    to_rank = str2double(s(end));
    if isempty(to_file_idx) || isnan(to_rank) || to_rank < 1 || to_rank > 8, return; end
    dest_c = to_file_idx; dest_r = to_rank;
    mid = s(1:end-2);
    disamb_file = 0; disamb_rank = 0;
    for i = 1:length(mid)
        if any(lower(mid(i)) == 'abcdefgh')
            disamb_file = find(files == lower(mid(i)), 1);
        elseif any(mid(i) == '12345678')
            disamb_rank = str2double(mid(i));
        end
    end
    legal = legal_moves(board, side, castling, ep);
    candidates = [];
    for k = 1:size(legal,1)
        m = legal(k,:);
        p = board(m(1),m(2));
        if abs(p) ~= piece_type, continue; end
        if p * side <= 0, continue; end
        if m(3) ~= dest_r || m(4) ~= dest_c, continue; end
        if disamb_file > 0 && m(2) ~= disamb_file, continue; end
        if disamb_rank > 0 && m(1) ~= disamb_rank, continue; end
        candidates = [candidates; m];
    end
    if isempty(candidates), return; end
    mv = candidates(1,:);
end

function show_analysis_window(history, title_str, use_mex)
    if isempty(history), return; end
    dlg = figure('Name', title_str, 'NumberTitle', 'off', 'MenuBar', 'none', ...
        'ToolBar', 'none', 'Position', [200 60 920 660], ...
        'Color', [0.12 0.12 0.14], 'Resize', 'on');
    uicontrol('Style','text','Parent',dlg,'Units','normalized', ...
        'Position',[0.02 0.93 0.5 0.05],'String','Move List','FontSize',13, ...
        'FontWeight','bold','BackgroundColor',[0.12 0.12 0.14], ...
        'ForegroundColor',[1 1 1],'HorizontalAlignment','left');
    list_str = build_list_strings(history);
    hList = uicontrol('Style','listbox','Parent',dlg,'Units','normalized', ...
        'Position',[0.02 0.14 0.48 0.78],'String',list_str,'FontSize',11, ...
        'FontName','Courier New','BackgroundColor',[0.18 0.18 0.20], ...
        'ForegroundColor',[1 1 1],'Value',numel(list_str),'Callback',@on_select);
    uicontrol('Style','text','Parent',dlg,'Units','normalized', ...
        'Position',[0.52 0.93 0.46 0.05],'String','Move Detail','FontSize',13, ...
        'FontWeight','bold','BackgroundColor',[0.12 0.12 0.14], ...
        'ForegroundColor',[1 1 1],'HorizontalAlignment','left');
    detail_box = uicontrol('Style','text','Parent',dlg,'Units','normalized', ...
        'Position',[0.52 0.28 0.46 0.64],'String','Chọn nước đi.', ...
        'FontSize',10,'FontName','Courier New', ...
        'BackgroundColor',[0.18 0.18 0.20],'ForegroundColor',[1 1 1], ...
        'HorizontalAlignment','left','VerticalAlignment','top');
    uicontrol('Style','pushbutton','Parent',dlg,'Units','normalized', ...
        'Position',[0.52 0.17 0.22 0.07],'String','Deep Analyze', ...
        'FontSize',11,'FontWeight','bold','Callback',@on_deep_analyze);
    uicontrol('Style','pushbutton','Parent',dlg,'Units','normalized', ...
        'Position',[0.76 0.17 0.22 0.07],'String','Analyze All', ...
        'FontSize',11,'Callback',@on_analyze_all);
    uicontrol('Style','pushbutton','Parent',dlg,'Units','normalized', ...
        'Position',[0.52 0.05 0.46 0.09],'String','Close','FontSize',12, ...
        'Callback',@(~,~) close(dlg));
    show_detail(get(hList,'Value'));

    function strs = build_list_strings(H)
        n=numel(H); strs=cell(n,1); mv_n=1;
        for k=1:n
            h=H(k);
            if h.side==1, pfx=sprintf('%d.',mv_n);
            else, pfx=sprintf('%d...',mv_n); mv_n=mv_n+1; end
            if h.analyzed && ~isempty(h.label)
                strs{k}=sprintf('%-6s %-8s  [%s]',pfx,h.san,h.label);
            else
                strs{k}=sprintf('%-6s %-8s  [ -- ]',pfx,h.san);
            end
        end
    end
    function show_detail(idx)
        if idx<1||idx>numel(history), return; end
        h=history(idx);
        mover=ternary(h.side==1,'White','Black');
        if h.analyzed && ~isempty(h.label)
            txt=sprintf(['Move #%d  (%s to move)\nSAN: %s       Coord: %s\n' ...
                'Classification: %s\nCP Loss: %.2f\n\n' ...
                'Best: %s   (%+.2f)\nPlayed: %s   (%+.2f)\n\n' ...
                'Position BEFORE the move:\n%s'], idx, mover, h.san, ...
                move_str(h.move), h.label, h.cp_loss/100, ...
                move_str(h.best), h.best_score/100, ...
                move_str(h.move), h.played_score/100, ...
                board_diagram(h.board));
            set(detail_box,'String',txt,'ForegroundColor',h.color);
        else
            txt=sprintf(['Move #%d  (%s to move)\nSAN: %s\n' ...
                'Coord: %s\n\nChưa phân tích.\n' ...
                'Nhấn "Deep Analyze" hoặc "Analyze All".\n\n' ...
                'Position BEFORE the move:\n%s'], ...
                idx, mover, h.san, move_str(h.move), board_diagram(h.board));
            set(detail_box,'String',txt,'ForegroundColor',[0.7 0.7 0.7]);
        end
    end
    function on_select(src,~), show_detail(get(src,'Value')); end
    function on_deep_analyze(~,~)
        idx=get(hList,'Value');
        if idx<1||idx>numel(history), return; end
        h=history(idx);
        set(detail_box,'String',sprintf('Deep analyzing %d...\n(~15s)',idx), ...
            'ForegroundColor',[1 0.9 0.3]); drawnow;
        try
            [lbl,col,loss,bm,bsc,psc]=deep_analyze(h.board,h.move,h.side, ...
                h.castling,h.ep,use_mex);
            history(idx).label=lbl; history(idx).color=col;
            history(idx).cp_loss=loss; history(idx).best=bm;
            history(idx).best_score=bsc; history(idx).played_score=psc;
            history(idx).analyzed=true;
            set(hList,'String',build_list_strings(history));
            set(hList,'Value',idx); show_detail(idx);
        catch err
            set(detail_box,'String',sprintf('Lỗi: %s',err.message), ...
                'ForegroundColor',[1 0.3 0.3]);
        end
    end
    function on_analyze_all(~,~)
        n=numel(history);
        wb=waitbar(0,'Analyzing all moves...');
        for k=1:n
            h=history(k);
            if h.analyzed && ~isempty(h.label)
                waitbar(k/n,wb,sprintf('%d/%d (cached)',k,n)); drawnow; continue;
            end
            try
                [lbl,col,loss,bm,bsc,psc]=deep_analyze(h.board,h.move,h.side, ...
                    h.castling,h.ep,use_mex);
                history(k).label=lbl; history(k).color=col;
                history(k).cp_loss=loss; history(k).best=bm;
                history(k).best_score=bsc; history(k).played_score=psc;
                history(k).analyzed=true;
            catch, end
            waitbar(k/n,wb,sprintf('%d/%d',k,n)); drawnow;
        end
        close(wb);
        set(hList,'String',build_list_strings(history));
        show_detail(get(hList,'Value'));
    end
end

function [y,dc]=disp_pos(r,c,flip)
    if flip, y=r; dc=9-c; else, y=9-r; dc=c; end
end

function s = fmt_time(t)
    if t<0, t=0; end
    m=floor(t/60); sec=floor(mod(t,60));
    if t<20, s=sprintf('%d:%04.1f',m,mod(t,60));
    else, s=sprintf('%02d:%02d',m,sec); end
end

function out = ternary(c,a,b), if c, out=a; else, out=b; end, end

function b = init_board()
    b=zeros(8);
    b(1,:)=[4 2 3 5 6 3 2 4]; b(2,:)=[1 1 1 1 1 1 1 1];
    b(7,:)=-[1 1 1 1 1 1 1 1]; b(8,:)=-[4 2 3 5 6 3 2 4];
end

function legal = legal_moves(board,side,castling,en_passant)
    pseudo=gen_moves(board,side,castling,en_passant);
    legal=zeros(size(pseudo,1),4); n=0;
    for i=1:size(pseudo,1)
        m=pseudo(i,:); p=board(m(1),m(2));
        is_castle=abs(p)==6&&abs(m(4)-m(2))==2; ok=true;
        if is_castle
            if is_in_check(board,side), ok=false; end
            if ok
                mid_c=(m(2)+m(4))/2;
                [nb1,~,~]=make_move(board,[m(1) m(2) m(1) mid_c],castling,en_passant,5);
                if is_in_check(nb1,side), ok=false; end
            end
        end
        if ok
            [nb,~,~]=make_move(board,m,castling,en_passant,5);
            if ~is_in_check(nb,side), n=n+1; legal(n,:)=m; end
        end
    end
    legal=legal(1:n,:);
end

function moves = gen_moves(board,side,castling,en_passant)
    moves=zeros(256,4); n=0;
    for r=1:8, for c=1:8
        p=board(r,c); if p*side<=0, continue; end
        t=abs(p);
        switch t
            case 1
                dir=side; nr=r+dir;
                if nr>=1&&nr<=8
                    if board(nr,c)==0
                        n=n+1; moves(n,:)=[r c nr c];
                        if (side==1&&r==2)||(side==-1&&r==7)
                            nr2=r+2*dir;
                            if board(nr2,c)==0, n=n+1; moves(n,:)=[r c nr2 c]; end
                        end
                    end
                    for dc=[-1 1]
                        nc=c+dc;
                        if nc>=1&&nc<=8
                            if board(nr,nc)*side<0, n=n+1; moves(n,:)=[r c nr nc];
                            elseif en_passant(1)==nr&&en_passant(2)==nc, n=n+1; moves(n,:)=[r c nr nc]; end
                        end
                    end
                end
            case 2
                for d=[1 2;2 1;-1 2;-2 1;1 -2;2 -1;-1 -2;-2 -1]'
                    nr=r+d(1); nc=c+d(2);
                    if nr>=1&&nr<=8&&nc>=1&&nc<=8&&board(nr,nc)*side<=0
                        n=n+1; moves(n,:)=[r c nr nc];
                    end
                end
            case 3
                for d=[1 1;1 -1;-1 1;-1 -1]'
                    nr=r+d(1); nc=c+d(2);
                    while nr>=1&&nr<=8&&nc>=1&&nc<=8
                        if board(nr,nc)*side>0, break; end
                        n=n+1; moves(n,:)=[r c nr nc];
                        if board(nr,nc)~=0, break; end
                        nr=nr+d(1); nc=nc+d(2);
                    end
                end
            case 4
                for d=[1 0;-1 0;0 1;0 -1]'
                    nr=r+d(1); nc=c+d(2);
                    while nr>=1&&nr<=8&&nc>=1&&nc<=8
                        if board(nr,nc)*side>0, break; end
                        n=n+1; moves(n,:)=[r c nr nc];
                        if board(nr,nc)~=0, break; end
                        nr=nr+d(1); nc=nc+d(2);
                    end
                end
            case 5
                for d=[1 0;-1 0;0 1;0 -1;1 1;1 -1;-1 1;-1 -1]'
                    nr=r+d(1); nc=c+d(2);
                    while nr>=1&&nr<=8&&nc>=1&&nc<=8
                        if board(nr,nc)*side>0, break; end
                        n=n+1; moves(n,:)=[r c nr nc];
                        if board(nr,nc)~=0, break; end
                        nr=nr+d(1); nc=nc+d(2);
                    end
                end
            case 6
                for d=[1 0;-1 0;0 1;0 -1;1 1;1 -1;-1 1;-1 -1]'
                    nr=r+d(1); nc=c+d(2);
                    if nr>=1&&nr<=8&&nc>=1&&nc<=8&&board(nr,nc)*side<=0
                        n=n+1; moves(n,:)=[r c nr nc];
                    end
                end
                if side==1&&r==1&&c==5
                    if castling(1)&&board(1,6)==0&&board(1,7)==0&&board(1,8)==4
                        n=n+1; moves(n,:)=[1 5 1 7];
                    end
                    if castling(2)&&board(1,4)==0&&board(1,3)==0&&board(1,2)==0&&board(1,1)==4
                        n=n+1; moves(n,:)=[1 5 1 3];
                    end
                end
                if side==-1&&r==8&&c==5
                    if castling(3)&&board(8,6)==0&&board(8,7)==0&&board(8,8)==-4
                        n=n+1; moves(n,:)=[8 5 8 7];
                    end
                    if castling(4)&&board(8,4)==0&&board(8,3)==0&&board(8,2)==0&&board(8,1)==-4
                        n=n+1; moves(n,:)=[8 5 8 3];
                    end
                end
        end
    end, end
    moves=moves(1:n,:);
end

function [nb,ncast,nep,captured] = make_move(b,m,castling,ep,promo)
    if nargin<5, promo=5; end
    nb=b; r1=m(1); c1=m(2); r2=m(3); c2=m(4);
    p=nb(r1,c1); captured=nb(r2,c2); nb(r2,c2)=p; nb(r1,c1)=0;
    if abs(p)==1&&c1~=c2&&captured==0, captured=nb(r1,c2); nb(r1,c2)=0; end
    if abs(p)==1&&(r2==8||r2==1), nb(r2,c2)=sign(p)*promo; end
    if abs(p)==6&&abs(c2-c1)==2
        if c2==7, nb(r1,6)=nb(r1,8); nb(r1,8)=0;
        else, nb(r1,4)=nb(r1,1); nb(r1,1)=0; end
    end
    ncast=castling;
    if p==6, ncast(1)=0; ncast(2)=0; end
    if p==-6, ncast(3)=0; ncast(4)=0; end
    if (r1==1&&c1==1)||(r2==1&&c2==1), ncast(2)=0; end
    if (r1==1&&c1==8)||(r2==1&&c2==8), ncast(1)=0; end
    if (r1==8&&c1==1)||(r2==8&&c2==1), ncast(4)=0; end
    if (r1==8&&c1==8)||(r2==8&&c2==8), ncast(3)=0; end
    nep=[0 0];
    if abs(p)==1&&abs(r2-r1)==2, nep=[(r1+r2)/2 c1]; end
end

function chk = is_in_check(board,side)
    chk=false; [kr,kc]=find(board==side*6,1);
    if isempty(kr), chk=true; return; end
    opp=-side;
    for dc=[-1 1]
        r=kr+side; c=kc+dc;
        if r>=1&&r<=8&&c>=1&&c<=8&&board(r,c)==opp*1, chk=true; return; end
    end
    for d=[1 2;2 1;-1 2;-2 1;1 -2;2 -1;-1 -2;-2 -1]'
        r=kr+d(1); c=kc+d(2);
        if r>=1&&r<=8&&c>=1&&c<=8&&board(r,c)==opp*2, chk=true; return; end
    end
    for d=[1 1;1 -1;-1 1;-1 -1]'
        r=kr+d(1); c=kc+d(2);
        while r>=1&&r<=8&&c>=1&&c<=8
            p=board(r,c);
            if p~=0
                if p==opp*3||p==opp*5, chk=true; return; end
                break;
            end
            r=r+d(1); c=c+d(2);
        end
    end
    for d=[1 0;-1 0;0 1;0 -1]'
        r=kr+d(1); c=kc+d(2);
        while r>=1&&r<=8&&c>=1&&c<=8
            p=board(r,c);
            if p~=0
                if p==opp*4||p==opp*5, chk=true; return; end
                break;
            end
            r=r+d(1); c=c+d(2);
        end
    end
    for d=[1 0;-1 0;0 1;0 -1;1 1;1 -1;-1 1;-1 -1]'
        r=kr+d(1); c=kc+d(2);
        if r>=1&&r<=8&&c>=1&&c<=8&&board(r,c)==opp*6, chk=true; return; end
    end
end

function score = evaluate(board)
    vals=[100 320 330 500 900 20000]; score=0; non_pawn=0;
    for r=1:8, for c=1:8
        t=abs(board(r,c)); if t>=2&&t<=5, non_pawn=non_pawn+vals(t); end
    end, end
    endgame=non_pawn<2500;
    for r=1:8, for c=1:8
        p=board(r,c); if p==0, continue; end
        side=sign(p); t=abs(p); score=score+side*vals(t);
        if t==6&&endgame, score=score+side*king_end(r,c,side);
        else, score=score+side*get_pst(t,r,c,side); end
    end, end
end

function v = get_pst(t,r,c,side)
    T=pst_table(t); if side==1, v=T(9-r,c); else, v=T(r,c); end
end

function v = king_end(r,c,side)
    T=[-50 -40 -30 -20 -20 -30 -40 -50; -30 -20 -10 0 0 -10 -20 -30;
       -30 -10 20 30 30 20 -10 -30; -30 -10 30 40 40 30 -10 -30;
       -30 -10 30 40 40 30 -10 -30; -30 -10 20 30 30 20 -10 -30;
       -30 -30 0 0 0 0 -30 -30; -50 -30 -30 -30 -30 -30 -30 -50];
    if side==1, v=T(9-r,c); else, v=T(r,c); end
end

function T = pst_table(type)
    persistent pawn knight bishop rook queen king
    if isempty(pawn)
        pawn=[0 0 0 0 0 0 0 0;50 50 50 50 50 50 50 50;10 10 20 30 30 20 10 10;
              5 5 10 25 25 10 5 5;0 0 0 20 20 0 0 0;5 -5 -10 0 0 -10 -5 5;
              5 10 10 -20 -20 10 10 5;0 0 0 0 0 0 0 0];
        knight=[-50 -40 -30 -30 -30 -30 -40 -50;-40 -20 0 0 0 0 -20 -40;
                -30 0 10 15 15 10 0 -30;-30 5 15 20 20 15 5 -30;
                -30 0 15 20 20 15 0 -30;-30 5 10 15 15 10 5 -30;
                -40 -20 0 5 5 0 -20 -40;-50 -40 -30 -30 -30 -30 -40 -50];
        bishop=[-20 -10 -10 -10 -10 -10 -10 -20;-10 0 0 0 0 0 0 -10;
                -10 0 5 10 10 5 0 -10;-10 5 5 10 10 5 5 -10;
                -10 0 10 10 10 10 0 -10;-10 10 10 10 10 10 10 -10;
                -10 5 0 0 0 0 5 -10;-20 -10 -10 -10 -10 -10 -10 -20];
        rook=[0 0 0 0 0 0 0 0;5 10 10 10 10 10 10 5;-5 0 0 0 0 0 0 -5;
              -5 0 0 0 0 0 0 -5;-5 0 0 0 0 0 0 -5;-5 0 0 0 0 0 0 -5;
              -5 0 0 0 0 0 0 -5;0 0 0 5 5 0 0 0];
        queen=[-20 -10 -10 -5 -5 -10 -10 -20;-10 0 0 0 0 0 0 -10;
               -10 0 5 5 5 5 0 -10;-5 0 5 5 5 5 0 -5;0 0 5 5 5 5 0 -5;
               -10 5 5 5 5 5 0 -10;-10 0 5 0 0 0 0 -10;-20 -10 -10 -5 -5 -10 -10 -20];
        king=[-30 -40 -40 -50 -50 -40 -40 -30;-30 -40 -40 -50 -50 -40 -40 -30;
              -30 -40 -40 -50 -50 -40 -40 -30;-30 -40 -40 -50 -50 -40 -40 -30;
              -20 -30 -30 -40 -40 -30 -30 -20;-10 -20 -20 -20 -20 -20 -20 -10;
              20 20 0 0 0 0 20 20;20 30 10 0 0 10 30 20];
    end
    switch type
        case 1, T=pawn; case 2, T=knight; case 3, T=bishop;
        case 4, T=rook; case 5, T=queen; case 6, T=king; otherwise, T=zeros(8);
    end
end

function [bm,bs,info] = find_best_matlab(board,side,castling,ep,depth,tlim)
    global ML_KILLERS ML_HISTORY ML_TSTART ML_TLIMIT ML_ABORT
    ML_KILLERS=cell(64,2);
    for i=1:64, ML_KILLERS{i,1}=zeros(1,4); ML_KILLERS{i,2}=zeros(1,4); end
    ML_HISTORY=zeros(8,8,8,8);
    ML_TSTART=tic; ML_TLIMIT=tlim; ML_ABORT=false;
    moves=legal_moves(board,side,castling,ep);
    bm=zeros(1,4); bs=0; info=struct('depth',0);
    if isempty(moves), return; end
    bm=moves(1,:);
    for d=1:depth
        [m,sc]=negamax_root(board,side,castling,ep,d);
        if ML_ABORT, break; end
        if ~isempty(m)&&any(m), bm=m; bs=sc; info.depth=d; end
        if abs(sc)>90000, break; end
        if toc(ML_TSTART)>ML_TLIMIT, break; end
    end
end

function [bm,bs] = negamax_root(board,side,castling,ep,depth)
    global ML_ABORT ML_TSTART ML_TLIMIT
    moves=legal_moves(board,side,castling,ep);
    bm=zeros(1,4); bs=-inf;
    if isempty(moves)
        if is_in_check(board,side), bs=-100000-depth; else, bs=0; end
        return;
    end
    moves=ml_order(board,moves,1); alpha=-inf; beta=inf;
    for i=1:size(moves,1)
        if ML_ABORT, bm=[]; bs=0; return; end
        if toc(ML_TSTART)>ML_TLIMIT, ML_ABORT=true; bm=[]; bs=0; return; end
        m=moves(i,:);
        [nb,nc,ne]=make_move(board,m,castling,ep,5);
        sc=-ml_negamax(nb,-side,nc,ne,depth-1,-beta,-alpha,2);
        if sc>bs, bs=sc; bm=m; end
        if sc>alpha, alpha=sc; end
    end
end

function sc = ml_negamax(board,side,castling,ep,depth,alpha,beta,ply)
    global ML_KILLERS ML_HISTORY ML_ABORT ML_TSTART ML_TLIMIT
    if ML_ABORT, sc=0; return; end
    if toc(ML_TSTART)>ML_TLIMIT, ML_ABORT=true; sc=0; return; end
    moves=legal_moves(board,side,castling,ep);
    if isempty(moves)
        if is_in_check(board,side), sc=-100000-depth; else, sc=0; end
        return;
    end
    if depth<=0, sc=ml_quiesce(board,side,castling,ep,alpha,beta,ply); return; end
    moves=ml_order(board,moves,ply); best=-inf; first=true;
    for i=1:size(moves,1)
        m=moves(i,:);
        [nb,nc,ne,cap]=make_move(board,m,castling,ep,5);
        if first, s=-ml_negamax(nb,-side,nc,ne,depth-1,-beta,-alpha,ply+1);
        else
            s=-ml_negamax(nb,-side,nc,ne,depth-1,-alpha-1,-alpha,ply+1);
            if s>alpha&&s<beta, s=-ml_negamax(nb,-side,nc,ne,depth-1,-beta,-alpha,ply+1); end
        end
        if s>best, best=s; end
        if s>alpha, alpha=s; end
        if alpha>=beta
            if cap==0
                k1=ML_KILLERS{ply,1};
                if ~isequal(k1,m), ML_KILLERS{ply,2}=k1; ML_KILLERS{ply,1}=m; end
                ML_HISTORY(m(1),m(2),m(3),m(4))=ML_HISTORY(m(1),m(2),m(3),m(4))+depth*depth;
            end
            break;
        end
        first=false;
    end
    sc=best;
end

function sc = ml_quiesce(board,side,castling,ep,alpha,beta,~)
    global ML_ABORT ML_TSTART ML_TLIMIT
    if ML_ABORT, sc=0; return; end
    if toc(ML_TSTART)>ML_TLIMIT, ML_ABORT=true; sc=0; return; end
    stand=evaluate(board)*side;
    if stand>=beta, sc=beta; return; end
    if stand>alpha, alpha=stand; end
    moves=legal_moves(board,side,castling,ep);
    caps=zeros(size(moves,1),4); n=0;
    for i=1:size(moves,1)
        m=moves(i,:);
        if board(m(3),m(4))~=0||(abs(board(m(1),m(2)))==1&&m(2)~=m(4))
            n=n+1; caps(n,:)=m;
        end
    end
    caps=caps(1:n,:); best=stand;
    for i=1:size(caps,1)
        m=caps(i,:);
        [nb,nc,ne]=make_move(board,m,castling,ep,5);
        s=-ml_quiesce(nb,-side,nc,ne,-beta,-alpha,0);
        if s>best, best=s; end
        if s>alpha, alpha=s; end
        if alpha>=beta, break; end
    end
    sc=best;
end

function moves = ml_order(board,moves,ply)
    global ML_KILLERS ML_HISTORY
    n=size(moves,1); if n<=1, return; end
    scores=zeros(n,1); k1=ML_KILLERS{ply,1}; k2=ML_KILLERS{ply,2};
    for i=1:n
        m=moves(i,:); cap=board(m(3),m(4));
        ep_=(abs(board(m(1),m(2)))==1)&&(m(2)~=m(4));
        if cap~=0||ep_
            vic=abs(cap); if vic==0, vic=1; end
            scores(i)=100000+vic*10-abs(board(m(1),m(2)));
        elseif isequal(m,k1), scores(i)=90000;
        elseif isequal(m,k2), scores(i)=89000;
        else, scores(i)=ML_HISTORY(m(1),m(2),m(3),m(4)); end
    end
    [~,idx]=sort(scores,'descend'); moves=moves(idx,:);
end

function img = render_board(s)
    sz=s.sq_size; N=sz*8; light=uint8([240 217 181]); dark=uint8([181 136 99]);
    img=uint8(zeros(N,N,3));
    for r=1:8, for c=1:8
        if mod(r+c,2)==0, col=light; else, col=dark; end
        if s.flip, dr=9-r; dc=9-c; else, dr=r; dc=c; end
        rs=(8-dr)*sz+1; cs=(dc-1)*sz+1;
        img(rs:rs+sz-1,cs:cs+sz-1,:)=repmat(reshape(col,1,1,3),sz,sz,1);
    end, end
    for r=1:8, for c=1:8
        p=s.board(r,c); if p==0, continue; end
        t=abs(p); if p>0, si=1; else, si=2; end
        spr=s.pieces{t,si};
        if s.flip, dr=9-r; dc=9-c; else, dr=r; dc=c; end
        rs=(8-dr)*sz+1; cs=(dc-1)*sz+1;
        sub=img(rs:rs+sz-1,cs:cs+sz-1,:);
        mask3=repmat(spr.alpha,1,1,3); sub(mask3)=spr.rgb(mask3);
        img(rs:rs+sz-1,cs:cs+sz-1,:)=sub;
    end, end
end

function P = build_all_sprites(sz)
    P=cell(6,2);
    for t=1:6, P{t,1}=make_piece_sprite(t,1,sz); P{t,2}=make_piece_sprite(t,-1,sz); end
end

function spr = make_piece_sprite(type,side,sz)
    m=piece_mask(type,sz); outline=dilate_mask(m,2);
    if side>0, fillc=[252 250 240]; linec=[20 20 20];
    else, fillc=[30 30 35]; linec=[220 220 220]; end
    rgb=uint8(zeros(sz,sz,3));
    for k=1:3
        ch=zeros(sz,sz,'uint8'); ch(outline)=linec(k); ch(m)=fillc(k); rgb(:,:,k)=ch;
    end
    spr=struct('rgb',rgb,'alpha',outline);
end

function d = dilate_mask(m,k)
    [X,Y]=meshgrid(-k:k,-k:k); se=double(X.^2+Y.^2<=k^2);
    d=conv2(double(m),se,'same')>0;
end

function m = piece_mask(type,sz)
    u=sz/64; m=false(sz,sz);
    switch type
        case 1
            m=m|mask_rect(sz,14*u,52*u,50*u,58*u);
            m=m|mask_poly(sz,[22 42 44 20]*u,[30 30 52 52]*u);
            m=m|mask_rect(sz,28*u,26*u,36*u,32*u);
            m=m|mask_circle(sz,32*u,20*u,9*u);
        case 2
            m=m|mask_rect(sz,14*u,52*u,50*u,58*u);
            m=m|mask_poly(sz,[20 20 24 28 34 42 48 46 40 38 42 44]*u,[52 42 32 22 14 12 20 28 34 40 46 52]*u);
        case 3
            m=m|mask_rect(sz,14*u,52*u,50*u,58*u);
            m=m|mask_rect(sz,18*u,46*u,46*u,52*u);
            m=m|mask_poly(sz,[24 24 28 32 36 40 40]*u,[46 34 22 16 22 34 46]*u);
            m=m|mask_circle(sz,32*u,14*u,4*u);
            m=m|mask_rect(sz,31*u,6*u,33*u,12*u);
            m=m|mask_rect(sz,28*u,8*u,36*u,10*u);
        case 4
            m=m|mask_rect(sz,14*u,52*u,50*u,58*u);
            m=m|mask_rect(sz,18*u,46*u,46*u,54*u);
            m=m|mask_rect(sz,20*u,24*u,44*u,52*u);
            m=m|mask_rect(sz,16*u,16*u,48*u,24*u);
            m=m&~mask_rect(sz,20*u,16*u,24*u,21*u);
            m=m&~mask_rect(sz,30*u,16*u,34*u,21*u);
            m=m&~mask_rect(sz,40*u,16*u,44*u,21*u);
        case 5
            m=m|mask_rect(sz,14*u,52*u,50*u,58*u);
            m=m|mask_rect(sz,18*u,46*u,46*u,52*u);
            m=m|mask_poly(sz,[24 22 42 40]*u,[46 32 32 46]*u);
            m=m|mask_rect(sz,20*u,26*u,44*u,32*u);
            for cx=[18 25 32 39 46], m=m|mask_circle(sz,cx*u,22*u,3.5*u); end
        case 6
            m=m|mask_rect(sz,14*u,52*u,50*u,58*u);
            m=m|mask_rect(sz,18*u,46*u,46*u,52*u);
            m=m|mask_poly(sz,[24 22 42 40]*u,[46 32 32 46]*u);
            m=m|mask_rect(sz,20*u,26*u,44*u,34*u);
            m=m|mask_rect(sz,29*u,10*u,35*u,28*u);
            m=m|mask_rect(sz,22*u,15*u,42*u,20*u);
    end
end

function m = mask_circle(sz,cx,cy,r)
    [X,Y]=meshgrid(1:sz,1:sz); m=(X-cx).^2+(Y-cy).^2<=r^2;
end
function m = mask_rect(sz,x1,y1,x2,y2)
    [X,Y]=meshgrid(1:sz,1:sz); m=X>=x1&X<=x2&Y>=y1&Y<=y2;
end
function m = mask_poly(sz,xv,yv)
    [X,Y]=meshgrid(1:sz,1:sz); m=logical(inpolygon(X,Y,xv,yv));
end

function choice = ask_promotion()
    choice=5;
    dlg=figure('Name','Promote pawn','NumberTitle','off','MenuBar','none', ...
        'ToolBar','none','Resize','off','Position',[560 400 340 110], ...
        'Color',[0.92 0.92 0.92],'WindowStyle','modal');
    uicontrol('Style','text','Parent',dlg,'Units','pixels','Position',[10 75 320 25], ...
        'String','Promote pawn to:','FontSize',12,'BackgroundColor',[0.92 0.92 0.92]);
    labels={'Queen','Rook','Bishop','Knight'}; vals=[5 4 3 2];
    for i=1:4
        uicontrol('Style','pushbutton','Parent',dlg,'Units','pixels', ...
            'Position',[10+(i-1)*82 20 76 40],'String',labels{i},'FontSize',12, ...
            'Callback',@(~,~) pick(vals(i)));
    end
    uiwait(dlg);
    function pick(v), choice=v; uiresume(dlg); end
    if ishandle(dlg), delete(dlg); end
end

function s = move_str(m)
    if isempty(m)||~any(m), s='-'; return; end
    files='abcdefgh';
    s=sprintf('%c%d%c%d',files(m(2)),m(1),files(m(4)),m(3));
end

function san = move_to_san(board,m,side,castling,ep)
    r1=m(1); c1=m(2); r2=m(3); c2=m(4);
    p=board(r1,c1); t=abs(p); files='abcdefgh';
    f_from=files(c1); f_to=files(c2); r_to=num2str(r2);
    cap=board(r2,c2)~=0; is_ep_cap=(t==1)&&(c1~=c2)&&(cap==0);
    if t==6&&abs(c2-c1)==2
        if c2==7, san='O-O'; else, san='O-O-O'; end
        [nb,ncast,nep]=make_move(board,m,castling,ep,5);
        if is_in_check(nb,-side)
            mv2=legal_moves(nb,-side,ncast,nep);
            if isempty(mv2), san=[san '#']; else, san=[san '+']; end
        end
        return;
    end
    if t==1, piece_letter=''; else, letters='PNBRQK'; piece_letter=letters(t); end
    disamb='';
    if t~=1&&t~=6
        allm=legal_moves(board,side,castling,ep);
        same_dest=allm(allm(:,3)==r2&allm(:,4)==c2,:);
        same_src=[];
        for k=1:size(same_dest,1)
            if abs(board(same_dest(k,1),same_dest(k,2)))==t&&~(same_dest(k,1)==r1&&same_dest(k,2)==c1)
                same_src=[same_src; same_dest(k,1:2)];
            end
        end
        if ~isempty(same_src)
            sf=any(same_src(:,2)==c1); sr=any(same_src(:,1)==r1);
            if ~sf, disamb=f_from;
            elseif ~sr, disamb=num2str(r1);
            else, disamb=[f_from num2str(r1)]; end
        end
    end
    if t==1
        if cap||is_ep_cap, san=[f_from 'x']; else, san=''; end
    else
        if cap, san=[piece_letter disamb 'x'];
        else, san=[piece_letter disamb]; end
    end
    san=[san f_to r_to];
    if t==1&&(r2==8||r2==1), san=[san '=Q']; end
    [nb,ncast,nep]=make_move(board,m,castling,ep,5);
    if is_in_check(nb,-side)
        mv2=legal_moves(nb,-side,ncast,nep);
        if isempty(mv2), san=[san '#']; else, san=[san '+']; end
    end
end

function s_str = board_diagram(board)
    letters='.PNBRQK'; lines=cell(10,1);
    lines{1}='    a b c d e f g h';
    for r=8:-1:1
        line=sprintf('  %d ',r);
        for c=1:8
            p=board(r,c);
            if p>0, line=[line letters(p+1) ' '];
            elseif p<0, line=[line lower(letters(-p+1)) ' '];
            else, line=[line '. ']; end
        end
        line=[line sprintf(' %d',r)];
        lines{9-r+1}=line;
    end
    lines{10}='    a b c d e f g h';
    s_str=strjoin(lines,sprintf('\n'));
end
