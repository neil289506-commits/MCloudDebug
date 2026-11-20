(function(){
  var root=document.documentElement;
  var tb=document.getElementById("theme");
  if(tb)tb.addEventListener("click",function(){
    var cur=root.dataset.theme||(matchMedia("(prefers-color-scheme: light)").matches?"light":"dark");
    var next=cur==="dark"?"light":"dark";
    root.dataset.theme=next;
    try{localStorage.setItem("theme",next)}catch(e){}
  });
  document.querySelectorAll(".lang a").forEach(function(a){a.addEventListener("click",function(){try{localStorage.setItem("lang",a.getAttribute("hreflang")==="zh-Hant"?"zh":a.getAttribute("hreflang"))}catch(e){}})});
  var mb=document.getElementById("menu"),nav=document.getElementById("nav");
  if(mb&&nav)mb.addEventListener("click",function(){
    var o=nav.classList.toggle("open");mb.setAttribute("aria-expanded",o);
  });
  document.querySelectorAll(".copy").forEach(function(b){
    b.addEventListener("click",function(){
      var t=b.parentNode.querySelector("code").innerText;
      var done=function(){var o=b.textContent;b.textContent=b.dataset.done||"OK";setTimeout(function(){b.textContent=o},1400)};
      if(navigator.clipboard)navigator.clipboard.writeText(t).then(done,function(){});
    });
  });
})();
