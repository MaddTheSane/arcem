	@
	@ $Id: hostfsfiler.s,v 1.1 2006/03/07 15:17:10 mhowkins Exp $
	@
	@ HostFS Filer
	@

	@ ARM constants
	VBIT = 1 << 28

	@ RISC OS constants
	XOS_CLI               = 0x20005
	XOS_Exit              = 0x20011
	XOS_Module            = 0x2001e
	XOS_ReadMonotonicTime = 0x20042
	XWimp_Initialise = 0x600c0
	XWimp_CreateIcon = 0x600c2
	XWimp_CloseDown  = 0x600dd
	XWimp_PollIdle   = 0x600e1

	Module_Enter = 2
	Module_Claim = 6
	Module_Free  = 7

	Message_Quit = 0

	Service_Reset             = 0x27
	Service_StartFiler        = 0x4b
	Service_StartedFiler      = 0x4c
	Service_FilerDying        = 0x4f

	WIMP_VERSION = 300

	WIMP_POLL_MASK = 0x00000031	@ no Null, Pointer Entering or Pointer Leaving events

	WORKSPACE_SIZE = 512

	WS_MY_TASK_HANDLE      = 0
	WS_FILER_TASK_HANDLE   = 4
	WS_WIMP_VERSION        = 8
	WS_ICON_BAR_BLOCK      = 12
	WS_WIMP_BLOCK          = 48 @ must be last



	.global	_start

_start:

	.int	start		@ Start
	.int	init		@ Initialisation
	.int	final		@ Finalisation
	.int	service		@ Service Call
	.int	title		@ Title String
	.int	help		@ Help String
	.int	table		@ Help and Command keyword table
	.int	0		@ SWI Chunk base
	.int	0		@ SWI handler code
	.int	0		@ SWI decoding table
	.int	0		@ SWI decoding code


title:
	.string	"ArcEmHostFSFiler"

help:
	.string	"HostFSFiler\t0.01 (07 Mar 2006)"
	.align


	@ Help and Command keyword table
table:
desktop_hostfsfiler:
	.string	"Desktop_HostFSFiler"
	.align
	.int	command_desktop_hostfsfiler
	.int	0x00070000
	.int	0
	.int	command_desktop_hostfsfiler_help

	.byte	0	@ Table terminator

command_desktop_hostfsfiler_help:
	.string	"The HostFSFiler provides the HostFS icons on the icon bar, and uses the Filer to display HostFS directories.\rDo not use *Desktop_HostFSFiler, use *Desktop instead."

	.align



init:
	stmfd	sp!, {lr}

	@ See if we need to claim some workspace
	ldr	r0, [r12]
	teq	r0, #0
	bne	1f

	@ Claim some workspace
	mov	r0, #Module_Claim
	mov	r3, #WORKSPACE_SIZE
	swi	XOS_Module
	ldmvsfd	sp!, {lr}
	orrvss	pc, lr, #VBIT

	str	r2, [r12]
1:
	ldr	r12, [r12]

	@ Initialise the workspace
	mov	r0, #0
	str	r0, [r12, #WS_MY_TASK_HANDLE]

	ldmfd	sp!, {pc}



final:
	stmfd	sp!, {lr}

	ldr	r12, [r12]

	@ Close Wimp task if active
	ldr	r0, [r12, #WS_MY_TASK_HANDLE]
	cmp	r0, #0
	ldrgt	r1, TASK
	swigt	XWimp_CloseDown

	@ Free workspace
	mov	r0, #Module_Free
	mov	r2, r12
	swi	XOS_Module

	ldmfd	sp!, {pc}^



service:
	teq	r1, #Service_Reset
	teqne	r1, #Service_StartFiler
	teqne	r1, #Service_StartedFiler
	teqne	r1, #Service_FilerDying
	movnes	pc, lr

	stmfd	sp!, {lr}

	ldr	r12, [r12]

	teq	r1, #Service_Reset
	beq	service_reset
	teq	r1, #Service_StartFiler
	beq	service_startfiler
	teq	r1, #Service_StartedFiler
	beq	service_startedfiler
	teq	r1, #Service_FilerDying
	beq	service_filerdying

	@ Should never reach here
	ldmfd	sp!, {pc}^



service_reset:
	@ Zero the Task Handle
	mov	lr, #0
	str	lr, [r12, #WS_MY_TASK_HANDLE]
	ldmfd	sp!, {pc}^



service_startfiler:
	ldr	lr, [r12, #WS_MY_TASK_HANDLE]
	teq	lr, #0			@ Am I already active?
	moveq	lr, #-1			@ No, so set handle to -1
	streq	lr, [r12, #WS_MY_TASK_HANDLE]
	streq	r1, [r12, #WS_FILER_TASK_HANDLE]	@ store Filer's task handle
	adreq	r0, desktop_hostfsfiler	@ r0 points to command to start task
	moveq	r1, #0			@ claim the service
	ldmfd	sp!, {pc}^



service_startedfiler:
	@ Zero the Task Handle if it is -1
	ldr	lr, [r12, #WS_MY_TASK_HANDLE]
	cmp	lr, #-1
	moveq	lr, #0
	streq	lr, [r12, #WS_MY_TASK_HANDLE]
	ldmfd	sp!, {pc}^



service_filerdying:
	@ Shut down task if active

	stmfd	sp!, {r0-r1}

	ldr	r0, [r12, #WS_MY_TASK_HANDLE]
	cmp	r0, #0

	@ Zero the Task Handle if non-zero
	movne	lr, #0
	strne	lr, [r12, #WS_MY_TASK_HANDLE]

	@ Shut down task if Task Handle was positive
	ldrgt	r1, TASK
	swigt	XWimp_CloseDown

	ldmfd	sp!, {r0-r1}
	ldmfd	sp!, {pc}^



command_desktop_hostfsfiler:
	stmfd	sp!, {lr}
	mov	r2, r0
	adr	r1, title
	mov	r0, #Module_Enter
	swi	XOS_Module
	ldmfd	sp!, {pc}



TASK:
	.ascii	"TASK"

task_title:
	.string	"HostFS Filer"
	.align

icon_bar_block:
	.int	-5		@ Left side of icon bar, scan from left (RO3+)
	.int	0		@ Minimum X
	.int	-16		@ Minimum Y
	.int	96		@ Maximum X
	.int	60		@ Maximum Y
	.int	0x1700310b	@ Flags (includes Indirected Text and Sprite)
	.int	0		@ Gap for pointer to Text
	.int	0		@ Gap for pointer to Validation String
	.int	6		@ Length of Text buffer

icon_bar_text:
	.string	"HostFS"

icon_bar_validation:
	.string	"Sharddisc"

	.align



	@ "Start" entry point
	@ Entered in User Mode
	@ Therefore no need to preserve link register before calling SWIs
start:
	ldr	r12, [r12]		@ Get workspace pointer
	ldr	r0, [r12, #WS_MY_TASK_HANDLE]
	cmp	r0, #0			@ Am I already active?
	ldrgt	r1, TASK		@ Yes, so close down first
	swigt	XWimp_CloseDown
	movgt	r0, #0			@ Mark as inactive
	strgt	r0, [r12, #WS_MY_TASK_HANDLE]

	ldr	r0, = WIMP_VERSION	@ (re)start the task
	ldr	r1, TASK
	adr	r2, task_title
	swi	XWimp_Initialise
	swivs	XOS_Exit		@ Exit if error

	str	r0, [r12, #WS_WIMP_VERSION]	@ store Wimp version
	str	r1, [r12, #WS_MY_TASK_HANDLE]	@ store Task handle


	@ Prepare block for Icon Bar icon
	adr	r0, icon_bar_block
	add	r1, r12, #WS_ICON_BAR_BLOCK

	ldmia	r0, {r2-r10}
	adr	r8, icon_bar_text		@ Fill in pointers
	adr	r9, icon_bar_validation
	stmia	r1, {r2-r10}


	@ Create Icon on Icon Bar
	mov	r0, #0x78000000		@ Priority higher than ADFS Hard Disc
	add	r1, r12, #WS_ICON_BAR_BLOCK
	swi	XWimp_CreateIcon
	bvs	close_down


	@ Main poll loop
re_poll:
	swi	XOS_ReadMonotonicTime	@ returns time in r0
	add	r2, r0, #100		@ poll no sooner than 1 sec unless event
	ldr	r0, = WIMP_POLL_MASK
	add	r1, r12, #WS_WIMP_BLOCK	@ point to Wimp block within workspace
	swi	XWimp_PollIdle
	bvs	close_down

	teq	r0, #6			@ 6 = Mouse Click
	beq	mouse_click
	teq	r0, #17			@ 17 = User Message
	teqne	r0, #18			@ 18 = User Message Recorded
	beq	user_message
	b	re_poll


mouse_click:
	ldr	r0, [r1, #12]		@ Icon handle
	cmp	r0, #-2
	bne	re_poll

	ldr	r0, [r1, #8]		@ Buttons
	cmp	r0, #4			@ Select
	cmpne	r0, #1			@ Adjust
	bne	re_poll

	adr	r0, cli_command
	swi	XOS_CLI

	b	re_poll

cli_command:
	.string	"Filer_OpenDir HostFS:$"
	.align


user_message:
	ldr	r0, [r1, #16]		@ Contains message code
	teq	r0, #Message_Quit	@ Is it Quit message...?
	bne	re_poll			@ ...no so re-poll
					@ otherwise continue to...
close_down:
	@ Close down Wimp task
	ldr	r0, [r12, #WS_MY_TASK_HANDLE]
	ldr	r1, TASK
	swi	XWimp_CloseDown

	@ Zero the Task Handle
	mov	r0, #0
	str	r0, [r12, #WS_MY_TASK_HANDLE]

	swi	XOS_Exit